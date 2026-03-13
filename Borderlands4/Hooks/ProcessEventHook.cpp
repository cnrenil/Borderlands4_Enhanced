#include "pch.h"


void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*) = nullptr;
void(*oPostRender)(SDK::UObject*, class SDK::UCanvas*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;

    // Use a very high throttle interval to avoid spamming, but enough to see it's alive
    // Logger::LogThrottled(Logger::Level::Debug, "PE", 10000, "hkProcessEvent: Alive and being called by engine");

	if (!Object || !Function || Cleaning.load() || bInsideHook) {
		Cheats::HandleDebugEvents(Object, Function, Params, oProcessEvent, true);
		g_ProcessEventCount.fetch_sub(1);
		return;
	}

	bInsideHook = true;
    try {
        if (Utils::bIsInGame) {
            // Block Input when Menu is open
            if (GUI::ShowMenu && Function) {
                // Use a more efficient check if possible, or just be careful with GetName()
                std::string fnName = Function->GetName();
                if (fnName.find("InputKey") != std::string::npos ||
                    fnName.find("InputAxis") != std::string::npos ||
                    fnName.find("InputTouch") != std::string::npos) 
                {
                    // CRITICAL FIX: To block, we must return WITHOUT calling oProcessEvent
                    bInsideHook = false;
                    Cheats::HandleDebugEvents(Object, Function, Params, oProcessEvent, false);
                    g_ProcessEventCount.fetch_sub(1);
                    return; 
                }
            }

            // Modular Handlers
            if (Cheats::HandleMovementEvents(Object, Function, Params)) goto Exit;
            if (Cheats::HandleWeaponEvents(Object, Function, Params)) goto Exit;
            if (Cheats::HandleCameraEvents(Object, Function, Params)) goto Exit;
        }
    }
	catch (...) {
		Logger::LogThrottled(Logger::Level::Error, "Hook", 1000, "CRASH in hkProcessEvent");
	}

Exit:
	bInsideHook = false;
	Cheats::HandleDebugEvents(Object, Function, Params, oProcessEvent, true);

	g_ProcessEventCount.fetch_sub(1);
}

void hkPostRender(UObject* ViewportClient, class UCanvas* Canvas)
{
	// Log throttled to confirm if this hook is even active
	Logger::LogThrottled(Logger::Level::Debug, "Hook", 10000, "hkPostRender: Hook called (Canvas: %p)", Canvas);

	if (oPostRender) oPostRender(ViewportClient, Canvas);
}

void Hooks::UnhookAll()
{
	LOG_INFO("Hook", "Unhooking all VTable hooks...");

	DWORD old;
	if (pcVTable && oProcessEvent)
	{
		if (VirtualProtect(&pcVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			pcVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&pcVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored PlayerController ProcessEvent.");
		}
	}

	if (cmVTable && oProcessEvent)
	{
		if (VirtualProtect(&cmVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			cmVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&cmVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored CameraManager ProcessEvent.");
		}
	}

	if (psVTable && oProcessEvent)
	{
		if (VirtualProtect(&psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			psVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&psVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored PlayerState ProcessEvent.");
		}
	}

	if (viewportVTable && oPostRender)
	{
		if (VirtualProtect(&viewportVTable[0x6D], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			viewportVTable[0x6D] = (void*)oPostRender;
			VirtualProtect(&viewportVTable[0x6D], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored ViewportClient PostRender.");
		}
	}
}

void** Hooks::pcVTable = nullptr;
void** Hooks::psVTable = nullptr;
void** Hooks::cmVTable = nullptr;
void** Hooks::viewportVTable = nullptr;

bool Hooks::HookProcessEvent()
{
	if (!GVars.PlayerController) return false;

	void** TempVTable = *reinterpret_cast<void***>(GVars.PlayerController);
	if (!TempVTable) return false;

	pcVTable = TempVTable;
	int processEventIdx = 73;

	if (TempVTable[processEventIdx] == &hkProcessEvent)
	{
		if (!oPostRender)
		{
			UWorld* World = Utils::GetWorldSafe();
			if (World && World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.Num() > 0)
			{
				UObject* ViewportClient = World->OwningGameInstance->LocalPlayers[0]->ViewportClient;
				if (ViewportClient)
				{
					Logger::Log(Logger::Level::Info, "Hook", "Found ViewportClient, attempting PostRender hook...");
					viewportVTable = *reinterpret_cast<void***>(ViewportClient);
                    int postRenderIndex = 0x6D;
                    if (viewportVTable && viewportVTable[postRenderIndex] != &hkPostRender)
                    {
                        oPostRender = reinterpret_cast<void(*)(SDK::UObject*, class SDK::UCanvas*)>(viewportVTable[postRenderIndex]);
                        DWORD oldP;
                        if (VirtualProtect(&viewportVTable[postRenderIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldP))
                        {
                            viewportVTable[postRenderIndex] = &hkPostRender;
                            VirtualProtect(&viewportVTable[postRenderIndex], sizeof(void*), oldP, &oldP);
                            LOG_INFO("Hook", "SUCCESS: PostRender Hooked!");
                        }
                        else {
                            Logger::Log(Logger::Level::Error, "Hook", "PostRender: VirtualProtect failed!");
                        }
                    }
				}
                else {
                    Logger::LogThrottled(Logger::Level::Info, "Hook", 10000, "PostRender ERROR: ViewportClient is NULL");
                }
			}
            else {
                Logger::LogThrottled(Logger::Level::Info, "Hook", 10000, "PostRender ERROR: GameInstance/LocalPlayers missing");
            }
		}
		return true;
	}

	oProcessEvent = reinterpret_cast<void(*)(const SDK::UObject*, SDK::UFunction*, void*)>(TempVTable[processEventIdx]);
	if (!oProcessEvent) return false;

	DWORD oldProtect;
	if (VirtualProtect(&TempVTable[processEventIdx], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		TempVTable[processEventIdx] = &hkProcessEvent;
		VirtualProtect(&TempVTable[processEventIdx], sizeof(void*), oldProtect, &oldProtect);
		LOG_DEBUG("Hook", "SUCCESS: VTable Overwritten globally for PlayerController!");
		return true;
	}
	return false;
}
