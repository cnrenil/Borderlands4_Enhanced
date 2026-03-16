#include "pch.h"

namespace
{
    static uintptr_t s_LastManagerAddress = 0;
    static double s_LastLookupTime = 0.0;
    static uintptr_t s_ActiveNumAddr = 0; // ALightProjectileManager + 0x3B0
    static int32_t s_LastObservedActiveNum = INT_MIN;

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

void Cheats::HandleDebugEvents(
    const SDK::UObject* Object,
    SDK::UFunction* Function,
    void* Params,
    void(*OriginalProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*),
    bool bCallOriginal)
{
    UpdateDebug();
    (void)Params;

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
