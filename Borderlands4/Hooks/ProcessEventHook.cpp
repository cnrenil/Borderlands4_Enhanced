#include "pch.h"

namespace
{
	Hooks::State g_HookState{};
	std::atomic<bool> g_LoggedHudCanvasHook{ false };
}

void(*oProcessEvent)(const UObject*, UFunction*, void*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

static bool TryRunGameThreadCanvasTick(UCanvas* Canvas)
{
	__try
	{
		Cheats::GameThreadCanvasTick(Canvas);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static UCanvas* TryGetHudCanvas(AHUD* Hud)
{
	__try
	{
		return Hud ? Hud->Canvas : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

static bool ShouldRunHudCanvasTick(const UObject* Object, const UFunction* Function, AHUD*& OutHud)
{
	OutHud = nullptr;
	if (!Object || !Function)
		return false;
	if (!Object->IsA(AHUD::StaticClass()))
		return false;
	if (Function->GetName() != "ReceiveDrawHUD")
		return false;

	OutHud = static_cast<AHUD*>(const_cast<UObject*>(Object));
	return OutHud != nullptr;
}

static void TryRunHudCanvasTick(AHUD* Hud)
{
	if (!Hud || Cleaning.load())
		return;

	UCanvas* Canvas = TryGetHudCanvas(Hud);
	if (Canvas)
	{
		if (!g_LoggedHudCanvasHook.exchange(true))
		{
			LOG_INFO("Hook", "ReceiveDrawHUD canvas hook active. Canvas=%p", Canvas);
		}
#if BL4_DEBUG_BUILD
		Logger::LogThrottled(Logger::Level::Debug, "Hook", 5000, "ReceiveDrawHUD canvas tick via ProcessEvent (Canvas: %p)", Canvas);
#endif
		TryRunGameThreadCanvasTick(Canvas);
	}
}

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;
	bool bSkipOriginal = false;
	bool bRunHudCanvasTick = false;
	AHUD* HudForCanvasTick = nullptr;
#if BL4_DEBUG_BUILD
    const bool bDebugEnabled = (ConfigManager::ConfigMap.count("Misc.Debug") && ConfigManager::B("Misc.Debug"));
#else
    const bool bDebugEnabled = false;
#endif
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

		if (Object->IsA(AHUD::StaticClass()) && Function->GetName() == "ReceiveDrawHUD")
		{
			HudForCanvasTick = static_cast<AHUD*>(const_cast<UObject*>(Object));
			bRunHudCanvasTick = (HudForCanvasTick != nullptr);
		}
    }
	catch (...) {
		Logger::LogThrottled(Logger::Level::Error, "Hook", 1000, "CRASH in hkProcessEvent");
	}

Exit:
	bInsideHook = false;
	DispatchDebugOrOriginal(!bSkipOriginal);

	if (!bSkipOriginal && bRunHudCanvasTick)
		TryRunHudCanvasTick(HudForCanvasTick);

	g_ProcessEventCount.fetch_sub(1);
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

	if (state.hudVTable && oProcessEvent)
	{
		if (VirtualProtect(&state.hudVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.hudVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&state.hudVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored HUD ProcessEvent.");
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
