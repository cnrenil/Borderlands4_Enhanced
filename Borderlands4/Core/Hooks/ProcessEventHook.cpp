#include "pch.h"

namespace
{
	Hooks::State g_HookState{};
}

void(*oProcessEvent)(const UObject*, UFunction*, void*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;
	bool bSkipOriginal = false;
#if BL4_DEBUG_BUILD
    const bool bDebugEnabled = (ConfigManager::ConfigMap.count("Misc.Debug") && ConfigManager::B("Misc.Debug"));
#else
    const bool bDebugEnabled = false;
#endif
    auto DispatchDebugOrOriginal = [&](bool bCallOriginal)
    {
        if (bDebugEnabled)
        {
            Features::Debug::HandleEvents(Object, Function, Params, oProcessEvent, bCallOriginal);
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
        std::string fnName = Function->GetName();

        if (Params && (fnName == "ClientSetCinematicMode" || fnName == "SetCinematicMode"))
        {
            bool bNewCinematicMode = false;
            if (fnName == "ClientSetCinematicMode")
            {
                const auto* cinematicParams = static_cast<const SDK::Params::PlayerController_ClientSetCinematicMode*>(Params);
                bNewCinematicMode = cinematicParams->bInCinematicMode;
            }
            else
            {
                const auto* cinematicParams = static_cast<const SDK::Params::PlayerController_SetCinematicMode*>(Params);
                bNewCinematicMode = cinematicParams->bInCinematicMode;
            }

            const bool bPreviousMode = Core::Scheduler::State().IsCinematicMode.exchange(bNewCinematicMode);
            if (bPreviousMode != bNewCinematicMode)
            {
                LOG_INFO("Overlay", "Cinematic mode %s. %s overlay rendering.", bNewCinematicMode ? "enabled" : "disabled", bNewCinematicMode ? "Suspending" : "Resuming");
            }
        }

        if (Core::Scheduler::State().CanRunGameplay()) {
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
            if (GUI::ShowMenu && !Core::Scheduler::State().ShouldSuspendOverlayRendering())
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
            if (Features::Data::bTriggerSuppressMouseInput.load() && IsMouseInputEvent(fnName))
            {
                bInsideHook = false;
                DispatchDebugOrOriginal(false);
                g_ProcessEventCount.fetch_sub(1);
                return;
            }

            if (ConfigManager::B("Player.Freecam") && ConfigManager::B("Misc.FreecamBlockInput"))
            {
                if (IsInputEvent(fnName))
                {
                    bInsideHook = false;
                    DispatchDebugOrOriginal(false);
                    g_ProcessEventCount.fetch_sub(1);
                    return;
                }
            }

            // Modular Handlers
            if (Core::Scheduler::DispatchEvent({ Object, Function, Params })) { bSkipOriginal = true; goto Exit; }
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

	if (state.psVTable && oProcessEvent)
	{
		if (VirtualProtect(&state.psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			state.psVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&state.psVTable[73], sizeof(void*), old, &old);
			LOG_INFO("Hook", "Restored PlayerState ProcessEvent.");
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
