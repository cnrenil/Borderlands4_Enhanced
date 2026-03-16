#include "pch.h"

#if !BL4_DEBUG_BUILD

void Cheats::HandleDebugEvents(
    const SDK::UObject* Object,
    SDK::UFunction* Function,
    void* Params,
    void(*OriginalProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*),
    bool bCallOriginal)
{
    if (bCallOriginal && OriginalProcessEvent)
    {
        OriginalProcessEvent(Object, Function, Params);
    }
}

void Cheats::DumpObjects()
{
}

void Cheats::UpdateDebug()
{
}

#else

namespace
{
    static uintptr_t s_LastManagerAddress = 0;
    static double s_LastLookupTime = 0.0;
    static uintptr_t s_ActiveNumAddr = 0; // ALightProjectileManager + 0x3B0
    static int32_t s_LastObservedActiveNum = INT_MIN;
    static double s_LastPostProcessTraceTime = 0.0;
    static std::unordered_map<std::string, uint64_t> s_RenderTraceSeen;

    void DumpPingTargetObject(SDK::AActor* TargetedActor)
    {
        if (!TargetedActor || !Utils::IsValidActor(TargetedActor))
            return;

        const std::string objectName = TargetedActor->GetName();
        const std::string fullName = TargetedActor->GetFullName();
        const std::string className = TargetedActor->Class ? TargetedActor->Class->GetName() : "None";
        const FVector location = TargetedActor->K2_GetActorLocation();

        FVector boundsOrigin{};
        FVector boundsExtent{};
        TargetedActor->GetActorBounds(false, &boundsOrigin, &boundsExtent, false);

        LOG_INFO("PingDump", "----------------------------------------");
        LOG_INFO("PingDump", "Target=%p Name=%s", TargetedActor, objectName.c_str());
        LOG_INFO("PingDump", "Class=%s", className.c_str());
        LOG_INFO("PingDump", "FullName=%s", fullName.c_str());
        LOG_INFO("PingDump", "Location=(%.2f, %.2f, %.2f)", (float)location.X, (float)location.Y, (float)location.Z);
        LOG_INFO("PingDump", "BoundsOrigin=(%.2f, %.2f, %.2f) BoundsExtent=(%.2f, %.2f, %.2f)",
            (float)boundsOrigin.X, (float)boundsOrigin.Y, (float)boundsOrigin.Z,
            (float)boundsExtent.X, (float)boundsExtent.Y, (float)boundsExtent.Z);

        if (TargetedActor->IsA(SDK::ACharacter::StaticClass()))
        {
            SDK::ACharacter* targetCharacter = static_cast<SDK::ACharacter*>(TargetedActor);
            if (targetCharacter->Mesh)
            {
                const int32_t numBones = targetCharacter->Mesh->GetNumBones();
                LOG_INFO("PingDump", "Mesh=%p NumBones=%d", targetCharacter->Mesh, numBones);

                const int32_t maxBonesToDump = (std::min)(numBones, 256);
                for (int32_t i = 0; i < maxBonesToDump; ++i)
                {
                    const FName boneName = targetCharacter->Mesh->GetBoneName(i);
                    const std::string boneNameStr = boneName.ToString();
                    if (boneNameStr.empty())
                        continue;

                    const FVector boneLocation = targetCharacter->Mesh->GetBoneTransform(boneName, ERelativeTransformSpace::RTS_World).Translation;
                    LOG_INFO("PingDump", "Bone[%d]=%s Pos=(%.2f, %.2f, %.2f)",
                        i,
                        boneNameStr.c_str(),
                        (float)boneLocation.X,
                        (float)boneLocation.Y,
                        (float)boneLocation.Z);
                }
            }
        }
    }

    bool TryReadInt32Noexcept(uintptr_t address, int32_t& outValue)
    {
        if (!address) return false;
        __try
        {
            outValue = *reinterpret_cast<volatile int32_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outValue = INT_MIN;
            return false;
        }
    }

    SDK::ALightProjectileManager* FindLightProjectileManagerForDebug()
    {
        SDK::UWorld* world = Utils::GetWorldSafe();
        if (!world) return nullptr;

        auto FindInLevel = [](SDK::ULevel* level) -> SDK::ALightProjectileManager*
        {
            if (!level) return nullptr;

            SDK::ALightProjectileManager* result = nullptr;
            Utils::ForEachLevelActor(level, [&](SDK::AActor* actor)
            {
                if (!actor || !actor->IsA(SDK::ALightProjectileManager::StaticClass()))
                    return true;

                result = static_cast<SDK::ALightProjectileManager*>(actor);
                return false;
            });
            return result;
        };

        if (SDK::ALightProjectileManager* mgr = FindInLevel(world->PersistentLevel))
            return mgr;

        for (int32 i = 0; i < world->StreamingLevels.Num(); ++i)
        {
            SDK::ULevelStreaming* streaming = world->StreamingLevels[i];
            if (!streaming) continue;
            if (SDK::ALightProjectileManager* mgr = FindInLevel(streaming->GetLoadedLevel()))
                return mgr;
        }

        return nullptr;
    }

    bool ContainsInsensitive(const std::string& text, const char* needle)
    {
        if (!needle || !needle[0]) return false;
        auto it = std::search(
            text.begin(), text.end(),
            needle, needle + std::strlen(needle),
            [](char lhs, char rhs)
            {
                return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
            });
        return it != text.end();
    }

    bool ShouldTraceRenderFunction(const SDK::UObject* Object, SDK::UFunction* Function)
    {
        if (!Object || !Function) return false;

        const std::string funcName = Function->GetName();
        const std::string className = Object->Class ? Object->Class->GetName() : "None";
        const std::string fullName = Object->GetFullName();

        static constexpr const char* kKeywords[] = {
            "PostProcess",
            "Blend",
            "Material",
            "CustomDepth",
            "Stencil",
            "RenderCustomDepth",
            "CreateDynamicMaterialInstance",
            "SetMaterial",
            "WeightedBlendable",
            "BlueprintModifyPostProcess"
        };

        for (const char* keyword : kKeywords)
        {
            if (ContainsInsensitive(funcName, keyword) || ContainsInsensitive(className, keyword) || ContainsInsensitive(fullName, keyword))
                return true;
        }

        return false;
    }

    void TraceRenderFunctionCall(const SDK::UObject* Object, SDK::UFunction* Function)
    {
        if (!Object || !Function) return;

        const std::string funcName = Function->GetName();
        const std::string className = Object->Class ? Object->Class->GetName() : "None";
        const std::string objectName = Object->GetName();
        const std::string key = className + "::" + funcName;
        const uint64_t nowMs = GetTickCount64();
        uint64_t& lastSeen = s_RenderTraceSeen[key];
        if (lastSeen != 0 && (nowMs - lastSeen) < 1000)
            return;

        lastSeen = nowMs;
        LOG_INFO("PPTrace", "PE %s::%s Obj=%s Ptr=%p", className.c_str(), funcName.c_str(), objectName.c_str(), Object);
    }

    void DumpPostProcessState()
    {
        if (!GVars.PlayerController || !GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
            return;

        SDK::AOakPlayerController* pc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
        if (!pc->PlayerCameraManager)
            return;

        SDK::APlayerCameraModeManager* cameraMode = nullptr;
        if (pc->PlayerCameraManager->IsA(SDK::APlayerCameraModeManager::StaticClass()))
            cameraMode = static_cast<SDK::APlayerCameraModeManager*>(pc->PlayerCameraManager);

        LOG_INFO(
            "PPTrace",
            "PCM=%p Class=%s CameraModeMgr=%p ModeState=%p",
            pc->PlayerCameraManager,
            pc->PlayerCameraManager->Class ? pc->PlayerCameraManager->Class->GetName().c_str() : "None",
            cameraMode,
            cameraMode ? cameraMode->CameraModeState : nullptr);

        if (cameraMode && cameraMode->CameraModeState)
        {
            LOG_INFO("PPTrace", "CameraModeState.PostProcessBlends.Num=%d", cameraMode->CameraModeState->PostProcessBlends.Num());
        }

        for (int32 i = 0; i < 8; ++i)
        {
            SDK::FPostProcessSettings settings{};
            float blendWeight = 0.0f;
            if (!SDK::USequenceCameraShakeTestUtil::GetPostProcessBlendCache(pc, i, &settings, &blendWeight))
                break;

            const auto& outline = settings.GbxOutlinePostProcessSettings;
            const auto& edge = settings.GbxEdgeDetectionPostProcessSettings;
            LOG_INFO(
                "PPTrace",
                "Blend[%d] Weight=%.3f Outline=%d Stencil=%d Thick=%.2f Edge=%d Sobel=%.2f",
                i,
                blendWeight,
                outline.OutlineTechEnable ? 1 : 0,
                outline.OutlineStencilTestEnable ? 1 : 0,
                outline.OutlineThickness,
                edge.EdgeDetectionEnable ? 1 : 0,
                edge.EdgeDetectionSobelPower);
        }
    }
}

void Cheats::HandleDebugEvents(
    const SDK::UObject* Object,
    SDK::UFunction* Function,
    void* Params,
    void(*OriginalProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*),
    bool bCallOriginal)
{
    UpdateDebug();

    if (Logger::IsRecording() && Object && Function)
    {
        const std::string FuncName = Function->GetName();
        const std::string ClassName = Object->Class ? Object->Class->GetName() : "None";
        const std::string ObjName = Object->GetName();
        if (ClassName.find("Widget") == std::string::npos && ClassName.find("Menu") == std::string::npos)
        {
            Logger::LogEvent(ClassName, FuncName, ObjName);
        }
    }

    if (ConfigManager::B("Misc.PingDump") && Object && Function && Params)
    {
        if (Object->IsA(SDK::AOakPlayerController::StaticClass()) && Function->GetName() == "ClientCreatePing")
        {
            auto* pingParams = static_cast<SDK::Params::OakPlayerController_ClientCreatePing*>(Params);
            if (pingParams->TargetedActor)
            {
                DumpPingTargetObject(pingParams->TargetedActor);
            }
        }
    }

    if (ConfigManager::B("Misc.PostProcessTrace") && ShouldTraceRenderFunction(Object, Function))
    {
        TraceRenderFunctionCall(Object, Function);
    }

    if (bCallOriginal && OriginalProcessEvent)
    {
        OriginalProcessEvent(Object, Function, Params);
    }
}


void Cheats::DumpObjects()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
	std::string logPath = dir + "\\GObjectsDump.txt";

	std::ofstream file(logPath);
	if (!file.is_open()) {
		LOG_ERROR("Dump", "Failed to open file for dumping: %s", logPath.c_str());
		return;
	}

	LOG_INFO("Dump", "Dumping all GObjects to %s...", logPath.c_str());

	int32_t count = 0;
	auto& GObjects = SDK::UObject::GObjects;

	for (int32_t i = 0; i < GObjects->Num(); i++)
	{
		SDK::UObject* Obj = GObjects->GetByIndex(i);
		if (!Obj) continue;

		try {
			std::string FullName = Obj->GetFullName();
			file << "[" << i << "] " << (void*)Obj << " | " << FullName << "\n";
			count++;
		}
		catch (...) {
		}
	}
	file.close();
	LOG_INFO("Dump", "Finished dumping %d objects.", count);
}

void Cheats::UpdateDebug()
{
    if (!Utils::bIsInGame) return;

    const double now = static_cast<double>(GetTickCount64()) * 0.001;
    if ((now - s_LastLookupTime) >= 0.25)
    {
        s_LastLookupTime = now;

        SDK::ALightProjectileManager* mgr = FindLightProjectileManagerForDebug();
        if (mgr)
        {
            const uintptr_t managerAddress = reinterpret_cast<uintptr_t>(mgr);
            if (managerAddress != s_LastManagerAddress)
            {
                s_LastManagerAddress = managerAddress;

                const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                const uintptr_t managerRva = (managerAddress >= imageBase) ? (managerAddress - imageBase) : 0;
                const uintptr_t activeArrayAddr = managerAddress + 0x3A8; // ALightProjectileManager::ActiveProjectiles
                const uintptr_t activeNumAddr = managerAddress + 0x3B0;   // TArray Num
                const uintptr_t activeMaxAddr = managerAddress + 0x3B4;   // TArray Max
                s_ActiveNumAddr = activeNumAddr;
                (void)TryReadInt32Noexcept(s_ActiveNumAddr, s_LastObservedActiveNum);

                Logger::Log(
                    Logger::Level::Info,
                    "DebugCE",
                    "LPM=%p rva=0x%llX ActiveArray=%p ActiveNum=%p ActiveMax=%p Num=%d",
                    reinterpret_cast<void*>(managerAddress),
                    static_cast<unsigned long long>(managerRva),
                    reinterpret_cast<void*>(activeArrayAddr),
                    reinterpret_cast<void*>(activeNumAddr),
                    reinterpret_cast<void*>(activeMaxAddr),
                    mgr->ActiveProjectiles.Num());
            }
        }
    }

    if (ConfigManager::B("Misc.PostProcessTrace"))
    {
        if ((now - s_LastPostProcessTraceTime) >= 1.0)
        {
            s_LastPostProcessTraceTime = now;
            DumpPostProcessState();
        }
    }
}

#endif
