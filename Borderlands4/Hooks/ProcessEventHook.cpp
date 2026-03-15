#include "pch.h"

namespace
{
	Hooks::State g_HookState{};
}

void(*oProcessEvent)(const UObject*, UFunction*, void*) = nullptr;
void(*oPostRender)(UObject*, class UCanvas*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;
	bool bSkipOriginal = false;
    const bool bDebugEnabled = (ConfigManager::ConfigMap.count("Misc.Debug") && ConfigManager::B("Misc.Debug"));
    auto DispatchDebugOrOriginal = [&](bool bCallOriginal)
    {
        if (bDebugEnabled)
        {
            Cheats::HandleDebugEvents(Object, Function, Params, oProcessEvent, bCallOriginal);
        }
        else if (bCallOriginal && oProcessEvent)
        {
            oProcessEvent(Object, Function, Params);
        }
    };

    // Use a very high throttle interval to avoid spamming, but enough to see it's alive
    // Logger::LogThrottled(Logger::Level::Debug, "PE", 10000, "hkProcessEvent: Alive and being called by engine");

	if (!Object || !Function || Cleaning.load() || bInsideHook) {
		DispatchDebugOrOriginal(true);
		g_ProcessEventCount.fetch_sub(1);
		return;
	}

	bInsideHook = true;
    try {
        if (Utils::bIsInGame) {
            std::string fnName = Function->GetName();
            auto IsInputEvent = [](const std::string& name) -> bool
            {
                return name.find("InputKey") != std::string::npos ||
                    name.find("InputAxis") != std::string::npos ||
                    name.find("InputTouch") != std::string::npos;
            };
            auto IsMouseInputEvent = [&](const std::string& name) -> bool
            {
                if (!IsInputEvent(name)) return false;
                if (name.find("Mouse") != std::string::npos) return true;
                if (name.find("LeftMouseButton") != std::string::npos) return true;
                if (name.find("RightMouseButton") != std::string::npos) return true;
                if (name.find("ThumbMouseButton") != std::string::npos) return true;
                if (name == "InputKey" || name == "InputAxis") return true;
                return false;
            };

            // Block Input when Menu is open
            if (GUI::ShowMenu)
            {
                if (IsInputEvent(fnName))
                {
                    // CRITICAL FIX: To block, we must return WITHOUT calling oProcessEvent
                    bInsideHook = false;
                    DispatchDebugOrOriginal(false);
                    g_ProcessEventCount.fetch_sub(1);
                    return; 
                }
            }

            // While triggerbot simulates mouse hold/taps, suppress game mouse input processing.
            if (Cheats::bTriggerSuppressMouseInput.load() && IsMouseInputEvent(fnName))
            {
                bInsideHook = false;
                DispatchDebugOrOriginal(false);
                g_ProcessEventCount.fetch_sub(1);
                return;
            }

            // Modular Handlers
            if (Cheats::HandleMovementEvents(Object, Function, Params)) { bSkipOriginal = true; goto Exit; }
            if (Cheats::HandleAimbotEvents(Object, Function, Params)) { bSkipOriginal = true; goto Exit; }
            if (Cheats::HandleWeaponEvents(Object, Function, Params)) { bSkipOriginal = true; goto Exit; }
            if (Cheats::HandleCameraEvents(Object, Function, Params)) { bSkipOriginal = true; goto Exit; }
        }
    }
	catch (...) {
		Logger::LogThrottled(Logger::Level::Error, "Hook", 1000, "CRASH in hkProcessEvent");
	}

Exit:
	bInsideHook = false;
	DispatchDebugOrOriginal(!bSkipOriginal);

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
	auto& state = GetState();

	DWORD old;
	if (state.pcVTable && oProcessEvent)
	{
		if (VirtualProtect(&state.pcVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.pcVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&state.pcVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored PlayerController ProcessEvent.");
		}
	}

	if (state.cmVTable && oProcessEvent)
	{
		if (VirtualProtect(&state.cmVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.cmVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&state.cmVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored CameraManager ProcessEvent.");
		}
	}

	if (state.psVTable && oProcessEvent)
	{
		if (VirtualProtect(&state.psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.psVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&state.psVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored PlayerState ProcessEvent.");
		}
	}

	if (state.viewportVTable && oPostRender)
	{
		if (VirtualProtect(&state.viewportVTable[0x6D], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.viewportVTable[0x6D] = (void*)oPostRender;
			VirtualProtect(&state.viewportVTable[0x6D], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored ViewportClient PostRender.");
		}
	}
}

Hooks::State& Hooks::GetState()
{
	return g_HookState;
}

bool Hooks::HookProcessEvent()
{
	if (!GVars.PlayerController) return false;

	void** TempVTable = *reinterpret_cast<void***>(GVars.PlayerController);
	if (!TempVTable) return false;

	auto& state = GetState();
	state.pcVTable = TempVTable;
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
					state.viewportVTable = *reinterpret_cast<void***>(ViewportClient);
                    int postRenderIndex = 0x6D;
                    if (state.viewportVTable && state.viewportVTable[postRenderIndex] != &hkPostRender)
                    {
                        oPostRender = reinterpret_cast<void(*)(UObject*, class UCanvas*)>(state.viewportVTable[postRenderIndex]);
                        DWORD oldP;
                        if (VirtualProtect(&state.viewportVTable[postRenderIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldP))
                        {
                            state.viewportVTable[postRenderIndex] = &hkPostRender;
                            VirtualProtect(&state.viewportVTable[postRenderIndex], sizeof(void*), oldP, &oldP);
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

	oProcessEvent = reinterpret_cast<void(*)(const UObject*, UFunction*, void*)>(TempVTable[processEventIdx]);
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
