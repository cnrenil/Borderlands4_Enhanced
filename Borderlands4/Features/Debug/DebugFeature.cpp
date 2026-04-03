#include "pch.h"
#include "DebugFeature.h"

#if !BL4_DEBUG_BUILD

namespace Features::Debug
{
    bool OnEvent(const Core::SchedulerGameEvent& Event)
    {
        return false;
    }

    void DumpObjects() {}
    void Update() {}
}

#else

namespace Features::Debug
{
    namespace
    {
        static uintptr_t s_LastManagerAddress = 0;
        static double s_LastLookupTime = 0.0;
        static uintptr_t s_ActiveNumAddr = 0; // ALightProjectileManager + 0x3B0
        static int32_t s_LastObservedActiveNum = INT_MIN;

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
    }

    bool OnEvent(const Core::SchedulerGameEvent& Event)
    {
        if (!ConfigManager::B("Misc.Debug")) return false;

        Update();

        if (Logger::IsRecording() && Event.Object && Event.Function)
        {
            const std::string FuncName = Event.Function->GetName();
            const std::string ClassName = Event.Object->Class ? Event.Object->Class->GetName() : "None";
            const std::string ObjName = Event.Object->GetName();
            if (ClassName.find("Widget") == std::string::npos && ClassName.find("Menu") == std::string::npos)
            {
                Logger::LogEvent(ClassName, FuncName, ObjName);
            }
        }

        if (ConfigManager::B("Misc.PingDump") && Event.Object && Event.Function && Event.Params)
        {
            if (Event.Object->IsA(SDK::AOakPlayerController::StaticClass()) && Event.Function->GetName() == "ClientCreatePing")
            {
                auto* pingParams = static_cast<SDK::Params::OakPlayerController_ClientCreatePing*>(Event.Params);
                if (pingParams->TargetedActor)
                {
                    DumpPingTargetObject(pingParams->TargetedActor);
                }
            }
        }

        return false;
    }

    void DumpObjects()
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

    void Update()
    {
        const double now = static_cast<double>(GetTickCount64()) * 0.001;
        if ((now - s_LastLookupTime) >= 0.25)
        {
            s_LastLookupTime = now;
        }
    }
}

#endif

namespace Features
{
    void RegisterDebugFeature()
    {
        Core::Scheduler::RegisterGameplayTickCallback("Debug", []()
        {
            Debug::Update();
        });

        Core::Scheduler::RegisterEventHandler("Debug", [](const Core::SchedulerGameEvent& Event)
        {
            return Debug::OnEvent(Event);
        });
    }
}
