#include "pch.h"

namespace
{
	Hooks::State g_HookState{};
}

void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;
	bool bSkipOriginal = false;

	if (!Object || !Function || Cleaning.load() || bInsideHook) {
		if (oProcessEvent) oProcessEvent(Object, Function, Params);
		g_ProcessEventCount.fetch_sub(1);
		return;
	}

	bInsideHook = true;
	try {
		if (Core::Scheduler::DispatchEvent({ Object, Function, Params })) {
			bSkipOriginal = true;
		}
	}
	catch (...) {
		Logger::LogThrottled(Logger::Level::Error, "Hook", 1000, "CRASH in hkProcessEvent");
	}

	bInsideHook = false;
	if (!bSkipOriginal && oProcessEvent) oProcessEvent(Object, Function, Params);

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
