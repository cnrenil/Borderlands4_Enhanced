#include "pch.h"

namespace
{
    static bool s_F7SessionActive = false;
    static bool s_F7WaitingForActiveNumChange = false;
    static int s_F7CurrentLevel = 0;
    static int s_F7SamplesAtLevel = 0;
    static int s_F7TargetSamplesPerLevel = 6;
    static int s_F7MaxLevels = 8;
    static int s_F7TimeoutsAtLevel = 0;
    static double s_F7WaitStartTime = 0.0;
    static double s_F7LastFireTime = 0.0;
    static std::vector<void*> s_F7LastBeforeStack;
    static std::unordered_map<uintptr_t, int> s_F7LevelCandidateHits;

    static uintptr_t s_LastManagerAddress = 0;
    static double s_LastLookupTime = 0.0;
    static uintptr_t s_ActiveNumAddr = 0; // ALightProjectileManager + 0x3B0
    static int32_t s_LastObservedActiveNum = INT_MIN;

    double GetNowSeconds()
    {
        return static_cast<double>(GetTickCount64()) * 0.001;
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

    bool ResolveAddressToModuleRva(void* address, char* outModule, size_t outModuleSize, unsigned long long& outRva)
    {
        if (!address || !outModule || outModuleSize == 0) return false;
        outModule[0] = '\0';
        outRva = 0;

        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module) || !module)
        {
            return false;
        }

        char modulePath[MAX_PATH]{};
        if (!GetModuleFileNameA(module, modulePath, MAX_PATH)) return false;

        const char* leaf = modulePath;
        for (const char* p = modulePath; *p; ++p)
        {
            if (*p == '\\' || *p == '/') leaf = p + 1;
        }

        strncpy_s(outModule, outModuleSize, leaf, _TRUNCATE);
        outRva = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module));
        return true;
    }

    USHORT CaptureStackFrames(void** outFrames, USHORT maxFrames, USHORT framesToSkip)
    {
        if (!outFrames || maxFrames == 0) return 0;
        return RtlCaptureStackBackTrace(framesToSkip + 1, maxFrames, outFrames, nullptr);
    }

    std::vector<void*> CaptureStackFramesVec(USHORT maxFrames, USHORT framesToSkip = 0)
    {
        void* stack[64]{};
        if (maxFrames > 64) maxFrames = 64;
        const USHORT captured = CaptureStackFrames(stack, maxFrames, framesToSkip + 1);
        return std::vector<void*>(stack, stack + captured);
    }

    bool IsMainModuleAddress(void* address)
    {
        if (!address) return false;
        const uintptr_t mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!mainBase) return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mainBase);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(mainBase + dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const uintptr_t mainEnd = mainBase + nt->OptionalHeader.SizeOfImage;

        const uintptr_t addr = reinterpret_cast<uintptr_t>(address);
        return addr >= mainBase && addr < mainEnd;
    }

    void LogTopFrameCandidates(const std::unordered_map<uintptr_t, int>& hits, const char* title, int topN, const char* channel = "DebugChain")
    {
        if (hits.empty() || !title || topN <= 0 || !channel) return;

        std::vector<std::pair<uintptr_t, int>> items;
        items.reserve(hits.size());
        for (const auto& kv : hits) items.push_back(kv);

        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b)
        {
            return a.second > b.second;
        });

        const int limit = std::min<int>(topN, static_cast<int>(items.size()));
        Logger::Log(Logger::Level::Info, channel, "%s (top %d)", title, limit);
        for (int i = 0; i < limit; ++i)
        {
            void* addr = reinterpret_cast<void*>(items[i].first);
            char mod[96]{};
            unsigned long long rva = 0;
            if (ResolveAddressToModuleRva(addr, mod, sizeof(mod), rva))
            {
                Logger::Log(Logger::Level::Info, channel, "  #%d hits=%d %s+0x%llX (%p)",
                    i + 1, items[i].second, mod, rva, addr);
            }
            else
            {
                Logger::Log(Logger::Level::Info, channel, "  #%d hits=%d %p",
                    i + 1, items[i].second, addr);
            }
        }
    }

    void LogStackFrames(const char* channel, const char* title, const std::vector<void*>& frames, int maxToLog = 12)
    {
        if (!channel || !title) return;
        Logger::Log(Logger::Level::Info, channel, "%s (frames=%u)", title, static_cast<unsigned>(frames.size()));

        const int limit = (std::min)(maxToLog, static_cast<int>(frames.size()));
        for (int i = 0; i < limit; ++i)
        {
            char moduleName[96]{};
            unsigned long long rva = 0;
            if (ResolveAddressToModuleRva(frames[static_cast<size_t>(i)], moduleName, sizeof(moduleName), rva))
            {
                Logger::Log(Logger::Level::Info, channel, "  #%02d %p %s+0x%llX",
                    i, frames[static_cast<size_t>(i)], moduleName, rva);
            }
            else
            {
                Logger::Log(Logger::Level::Info, channel, "  #%02d %p",
                    i, frames[static_cast<size_t>(i)]);
            }
        }
    }

    void* FindMainModuleFrameByLevel(const std::vector<void*>& frames, int level)
    {
        if (level < 0) return nullptr;
        int currentLevel = 0;
        for (void* frame : frames)
        {
            if (!IsMainModuleAddress(frame)) continue;
            if (currentLevel == level) return frame;
            ++currentLevel;
        }
        return nullptr;
    }

    void ResetF7LevelSamples()
    {
        s_F7SamplesAtLevel = 0;
        s_F7TimeoutsAtLevel = 0;
        s_F7LastBeforeStack.clear();
        s_F7LevelCandidateHits.clear();
    }

    void StopF7Session(const char* reason)
    {
        Logger::Log(Logger::Level::Info, "DebugF7", "F7 trace stopped: %s", reason ? reason : "No reason");
        s_F7SessionActive = false;
        s_F7WaitingForActiveNumChange = false;
        s_F7CurrentLevel = 0;
        ResetF7LevelSamples();
    }

    bool ValidateLocalCharacterForF7()
    {
        if (!Utils::bIsInGame) return false;
        SDK::AActor* self = Utils::GetSelfActor();
        if (!self) return false;
        return Utils::IsValidActor(self);
    }

    void SimulateFireForF7Probe()
    {
        if (!s_F7SessionActive || s_F7WaitingForActiveNumChange) return;
        if (!ValidateLocalCharacterForF7())
        {
            StopF7Session("LocalCharacter unavailable");
            return;
        }

        s_F7LastBeforeStack = CaptureStackFramesVec(32, 0);
        LogStackFrames("DebugF7", "Before fire stack", s_F7LastBeforeStack);

        Utils::SendMouseLeftDown();
        Utils::SendMouseLeftUp();

        s_F7WaitingForActiveNumChange = true;
        s_F7WaitStartTime = GetNowSeconds();
        s_F7LastFireTime = s_F7WaitStartTime;
        Logger::Log(Logger::Level::Info, "DebugF7",
            "F7 sample shot sent. level=%d sample=%d/%d",
            s_F7CurrentLevel,
            s_F7SamplesAtLevel + 1,
            s_F7TargetSamplesPerLevel);
    }

    void FinalizeF7CurrentLevel()
    {
        if (s_F7SamplesAtLevel <= 0)
        {
            StopF7Session("No valid samples captured at this level");
            return;
        }

        LogTopFrameCandidates(s_F7LevelCandidateHits, "F7 level candidates", 5, "DebugF7");

        uintptr_t bestAddr = 0;
        int bestHits = 0;
        for (const auto& kv : s_F7LevelCandidateHits)
        {
            if (kv.second > bestHits)
            {
                bestAddr = kv.first;
                bestHits = kv.second;
            }
        }

        if (!bestAddr || bestHits <= 0)
        {
            StopF7Session("No stable candidate frame found");
            return;
        }

        char mod[96]{};
        unsigned long long rva = 0;
        if (ResolveAddressToModuleRva(reinterpret_cast<void*>(bestAddr), mod, sizeof(mod), rva))
        {
            Logger::Log(Logger::Level::Info, "DebugF7",
                "Level %d best candidate: %s+0x%llX (%p), hits=%d/%d",
                s_F7CurrentLevel, mod, rva, reinterpret_cast<void*>(bestAddr), bestHits, s_F7SamplesAtLevel);
        }
        else
        {
            Logger::Log(Logger::Level::Info, "DebugF7",
                "Level %d best candidate: %p, hits=%d/%d",
                s_F7CurrentLevel, reinterpret_cast<void*>(bestAddr), bestHits, s_F7SamplesAtLevel);
        }

        if ((s_F7CurrentLevel + 1) >= s_F7MaxLevels)
        {
            StopF7Session("Reached configured max recursive levels");
            return;
        }

        ++s_F7CurrentLevel;
        ResetF7LevelSamples();
        Logger::Log(Logger::Level::Info, "DebugF7", "Advancing to upper level %d", s_F7CurrentLevel);
        SimulateFireForF7Probe();
    }

    void TickF7SessionTimeout()
    {
        if (!s_F7SessionActive) return;
        const double now = GetNowSeconds();

        if (s_F7WaitingForActiveNumChange)
        {
            if ((now - s_F7WaitStartTime) >= 1.0)
            {
                s_F7WaitingForActiveNumChange = false;
                ++s_F7TimeoutsAtLevel;
                Logger::Log(Logger::Level::Warning, "DebugF7",
                    "Sample timeout (no ActiveNum change). level=%d timeout=%d",
                    s_F7CurrentLevel, s_F7TimeoutsAtLevel);

                if (s_F7TimeoutsAtLevel >= 3 && s_F7SamplesAtLevel == 0)
                {
                    StopF7Session("Unable to produce projectile activity");
                    return;
                }
            }
        }

        if (!s_F7WaitingForActiveNumChange &&
            s_F7SamplesAtLevel < s_F7TargetSamplesPerLevel &&
            (now - s_F7LastFireTime) >= 0.05)
        {
            SimulateFireForF7Probe();
        }

        if (!s_F7WaitingForActiveNumChange &&
            s_F7SamplesAtLevel >= s_F7TargetSamplesPerLevel)
        {
            FinalizeF7CurrentLevel();
        }
    }

    SDK::ALightProjectileManager* FindLightProjectileManagerForDebug()
    {
        SDK::UWorld* world = Utils::GetWorldSafe();
        if (!world) return nullptr;

        auto FindInLevel = [](SDK::ULevel* level) -> SDK::ALightProjectileManager*
        {
            if (!level) return nullptr;
            const int32 actorCount = level->Actors.Num();
            if (actorCount <= 0 || actorCount > 200000) return nullptr;

            for (int32 i = 0; i < actorCount; ++i)
            {
                SDK::AActor* actor = level->Actors[i];
                if (!actor) continue;
                if (!actor->IsA(SDK::ALightProjectileManager::StaticClass())) continue;
                return static_cast<SDK::ALightProjectileManager*>(actor);
            }
            return nullptr;
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

void Cheats::DebugF7Handler()
{
    if (!ValidateLocalCharacterForF7())
    {
        Logger::Log(Logger::Level::Warning, "DebugF7",
            "F7 ignored: LocalCharacter not in playable map yet.");
        return;
    }

    s_F7SessionActive = true;
    s_F7WaitingForActiveNumChange = false;
    s_F7CurrentLevel = 0;
    ResetF7LevelSamples();

    Logger::Log(Logger::Level::Info, "DebugF7",
        "=== F7 TRACE START === samplesPerLevel=%d maxLevels=%d",
        s_F7TargetSamplesPerLevel, s_F7MaxLevels);
    SimulateFireForF7Probe();
}

void Cheats::HandleDebugEvents(
    const SDK::UObject* Object,
    SDK::UFunction* Function,
    void* Params,
    void(*OriginalProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*),
    bool bCallOriginal)
{
    UpdateDebug();
    TickF7SessionTimeout();
    (void)Params;

    int32_t beforeActiveNum = INT_MIN;
    const bool bHaveBeforeActiveNum = TryReadInt32Noexcept(s_ActiveNumAddr, beforeActiveNum);

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

    if (bCallOriginal && OriginalProcessEvent)
    {
        OriginalProcessEvent(Object, Function, Params);
    }

    if (bHaveBeforeActiveNum && Object && Function && Object->Class)
    {
        int32_t afterActiveNum = INT_MIN;
        if (TryReadInt32Noexcept(s_ActiveNumAddr, afterActiveNum) && afterActiveNum != beforeActiveNum)
        {
            s_LastObservedActiveNum = afterActiveNum;
            void* stack[32]{};
            if (s_F7SessionActive && s_F7WaitingForActiveNumChange)
            {
                s_F7WaitingForActiveNumChange = false;
                const USHORT captured = CaptureStackFrames(stack, 32, 0);
                std::vector<void*> afterStack(stack, stack + captured);
                LogStackFrames("DebugF7", "After fire stack", afterStack);

                void* levelFrame = FindMainModuleFrameByLevel(afterStack, s_F7CurrentLevel);
                if (levelFrame)
                {
                    ++s_F7LevelCandidateHits[reinterpret_cast<uintptr_t>(levelFrame)];
                    char mod[96]{};
                    unsigned long long rva = 0;
                    ResolveAddressToModuleRva(levelFrame, mod, sizeof(mod), rva);
                    Logger::Log(Logger::Level::Info, "DebugF7",
                        "Sample accepted. level=%d frame=%s+0x%llX (%p)",
                        s_F7CurrentLevel, mod, rva, levelFrame);
                }
                else
                {
                    Logger::Log(Logger::Level::Warning, "DebugF7",
                        "Sample accepted but no module frame at level=%d", s_F7CurrentLevel);
                }

                ++s_F7SamplesAtLevel;
                s_F7TimeoutsAtLevel = 0;
                Logger::Log(Logger::Level::Info, "DebugF7",
                    "Level %d progress: %d/%d",
                    s_F7CurrentLevel, s_F7SamplesAtLevel, s_F7TargetSamplesPerLevel);
            }
        }
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
    if ((now - s_LastLookupTime) < 0.25) return;
    s_LastLookupTime = now;

    SDK::ALightProjectileManager* mgr = FindLightProjectileManagerForDebug();
    if (!mgr) return;

    const uintptr_t managerAddress = reinterpret_cast<uintptr_t>(mgr);
    if (managerAddress == s_LastManagerAddress) return;
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
