#include "pch.h"

extern HWND g_hWnd;
extern HWND g_hTrackedWindow;
extern HWND g_hConsoleWnd;
extern WNDPROC oWndProc;
extern std::atomic<bool> Resizing;
extern std::atomic<bool> Cleaning;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params);

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (Cleaning.load())
		return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);

	switch (uMsg) {
		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED) Resizing.store(true);
			break;
		case WM_EXITSIZEMOVE:
			Resizing.store(false);
			break;
		case WM_CLOSE:
		case WM_DESTROY:
		case WM_QUIT:
			ExitProcess(0);
			break;
	}

    // Serialize Win32 message ingestion with the render thread. ImGui internal
    // containers are not thread-safe and this hook does not run on the Present thread.
    GUI::Overlay::HandleWndProcMessage(hWnd, uMsg, wParam, lParam);

	if (GUI::ShowMenu && !Utils::ShouldSuspendOverlayRendering()) {
		// Fully block user input from reaching the game when menu is open.
		switch (uMsg) {
			case WM_INPUT:
			case WM_MOUSEMOVE:
			case WM_MOUSELEAVE:
			case WM_NCMOUSEMOVE:
			case WM_NCMOUSELEAVE:
			case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
			case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
			case WM_CAPTURECHANGED:
			case WM_SETFOCUS:
			case WM_KILLFOCUS:
			case WM_KEYDOWN: case WM_SYSKEYDOWN:
			case WM_KEYUP: case WM_SYSKEYUP:
			case WM_SYSCHAR:
			case WM_CHAR:
			case WM_DEADCHAR:
			case WM_SYSDEADCHAR:
				return 0;
		}
	}

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void Cleanup(HMODULE hModule)
{
	Cleaning.store(true);
	d3d12hook::release();
	Cheats::ShutdownCamera();
	
	// Restore WndProc
	if (g_hWnd && oWndProc) {
		SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
	}
	
	Hooks::UnhookAll();
	MH_Uninitialize();
	
	Logger::Shutdown();
	if (g_hConsoleWnd && IsWindow(g_hConsoleWnd))
	{
		PostMessage(g_hConsoleWnd, WM_CLOSE, 0, 0);
	}
	FreeConsole();
	g_hConsoleWnd = nullptr;

	FreeLibraryAndExitThread(hModule, 0);
}

// Wrapper for SEH to avoid C2712
static void InternalUpdateHooksSEH(bool& bIsProcessEventHooked, bool& bIsPlayerStateHooked, bool& bIsCameraManagerHooked)
{
	auto& hookState = Hooks::GetState();
	__try {
		if (!bIsProcessEventHooked && GVars.PlayerController)
		{
			bIsProcessEventHooked = Hooks::HookProcessEvent();
		}

		if (bIsPlayerStateHooked)
		{
			if (GVars.Character && GVars.Character->PlayerState)
			{
				void** currentPSVTable = *reinterpret_cast<void***>(GVars.Character->PlayerState);
				if (currentPSVTable && currentPSVTable != hookState.psVTable)
				{
					bIsPlayerStateHooked = false; 
				}
			}
			else
			{
				bIsPlayerStateHooked = false;
			}
		}

		bIsCameraManagerHooked = false;

		if (bIsProcessEventHooked)
		{
			if (!bIsPlayerStateHooked && GVars.Character && GVars.Character->PlayerState)
			{
				void** psVTable = *reinterpret_cast<void***>(GVars.Character->PlayerState);
				if (psVTable && !IsBadReadPtr(psVTable, sizeof(void*) * 80)) 
				{
					if (psVTable[73] != &hkProcessEvent) {
						DWORD old;
						if (VirtualProtect(&psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
							psVTable[73] = (void*)hkProcessEvent;
							VirtualProtect(&psVTable[73], sizeof(void*), old, &old);
							
							hookState.psVTable = psVTable; 
							bIsPlayerStateHooked = true;
							// LOG_INFO moved outside because SEH can't mix with RAII
						}
					}
					else {
						hookState.psVTable = psVTable;
						bIsPlayerStateHooked = true;
					}
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		bIsPlayerStateHooked = false;
		bIsCameraManagerHooked = false;
	}
}

static void SafeUpdateHooks(bool& bIsProcessEventHooked, bool& bIsPlayerStateHooked, bool& bIsCameraManagerHooked)
{
	bool prevPS = bIsPlayerStateHooked;
	bool prevCM = bIsCameraManagerHooked;

	InternalUpdateHooksSEH(bIsProcessEventHooked, bIsPlayerStateHooked, bIsCameraManagerHooked);

	// Log success outside SEH to allow using std::string conversion in LOG_DEBUG
	if (!prevPS && bIsPlayerStateHooked) LOG_DEBUG("Hook", "SUCCESS: PlayerState ProcessEvent Hooked!");

	// Logger::LogThrottled(Logger::Level::Debug, "Hook", 10000, "SafeUpdateHooks: Hooks Status (PE: %d, PS: %d, CM: %d)", bIsProcessEventHooked, bIsPlayerStateHooked, bIsCameraManagerHooked);
}

static void AutoSetVariablesLocked()
{
	SDK::UWorld* currentWorld = Utils::GetWorldSafe();
	SDK::UWorld* trackedWorld = nullptr;
	{
		std::scoped_lock GVarsLock(gGVarsMutex);
		trackedWorld = GVars.World;
	}

	if (trackedWorld && trackedWorld != currentWorld)
	{
		Cheats::ShutdownCamera();
	}
	else if (trackedWorld)
	{
		SDK::APlayerController* currentPlayerController = nullptr;
		SDK::ACharacter* currentCharacter = nullptr;
		if (currentWorld)
		{
			currentPlayerController = Utils::GetPlayerController();
			if (currentPlayerController && !IsBadReadPtr(currentPlayerController, sizeof(void*)) && currentPlayerController->VTable)
			{
				currentCharacter = currentPlayerController->Character;
			}
		}

		if (!currentWorld || !currentPlayerController || !currentCharacter)
		{
			Cheats::ShutdownCamera();
		}
	}

	{
		std::scoped_lock GVarsLock(gGVarsMutex);
		GVars.AutoSetVariables();
	}
}

static bool TryAutoSetVariablesMainThread()
{
	__try
	{
		AutoSetVariablesLocked();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		GVars.Reset();
		Utils::bIsLoading = true;
		Utils::bIsInGame = false;
		return false;
	}
}

DWORD MainThread(HMODULE hModule)
{
	AllocConsole();
	g_hConsoleWnd = GetConsoleWindow();
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

    Logger::Initialize();
	LOG_INFO("System", "Cheat Injecting...");

	LOG_INFO("System", "Executing Anti-Debug Bypass...");
	AntiDebug::Bypass();

	MH_STATUS Status = MH_Initialize();
	if (Status != MH_OK)
	{
		LOG_ERROR("MinHook", "MinHook failed to init: %d", (int)Status);
		Cleanup(hModule);
	}

	LOG_INFO("System", "Loading configurations...");
	Localization::Initialize();
    HotkeyManager::Initialize();

	ConfigManager::LoadSettings();

	LOG_INFO("System", "Waiting for stable game state...");
	Sleep(2000); 

	bool bIsProcessEventHooked = false;
	bool bIsPlayerStateHooked = false;
	bool bIsCameraManagerHooked = false;
	bool bDx12Hooked = false;
	DWORD lastDx12HookAttemptTick = 0;
	bool hadGameWindow = false;
	DWORD missingWindowSinceTick = 0;

	while (!Cleaning.load())
	{
		if (g_hTrackedWindow && IsWindow(g_hTrackedWindow))
		{
			hadGameWindow = true;
			missingWindowSinceTick = 0;
		}
		else if (hadGameWindow && !Resizing.load())
		{
			DWORD now = GetTickCount();
			if (missingWindowSinceTick == 0)
			{
				missingWindowSinceTick = now;
			}
			else if ((now - missingWindowSinceTick) > 5000)
			{
				LOG_INFO("System", "Game window remained unavailable. Cleaning up overlay thread.");
				break;
			}
		}
		else
		{
			missingWindowSinceTick = 0;
		}

		// 1. Always keep variables updated until PostRender takes over
        // This prevents the 'PlayerController found but logic stopped' deadlock
		if (!TryAutoSetVariablesMainThread())
			Logger::LogThrottled(Logger::Level::Warning, "System", 1000, "MainThread: AutoSetVariables exception, resetting transient state");

		// 1.5 Delay DX12 hook installation until world state is available.
		// Hooking too early (splash/window transition phase) can destabilize Present.
		if (!bDx12Hooked && GVars.World)
		{
			DWORD now = GetTickCount();
			if (lastDx12HookAttemptTick == 0 || (now - lastDx12HookAttemptTick) > 2000)
			{
				lastDx12HookAttemptTick = now;
				LOG_DEBUG("System", "Stable world detected. Installing DX12 hooks...");
				if (Engine::HookPresent())
				{
					bDx12Hooked = true;
					LOG_INFO("System", "Engine hooks initialized successfully.");
				}
				else
				{
					LOG_WARN("System", "DX12 hook install attempt failed; will retry...");
				}
			}
		}
		
		// 2. Always attempt to stabilize hooks (PE, PS, CM)
        // We remove the !Utils::bIsLoading check here to allow HookProcessEvent() 
        // to finish hooking PostRender even if actors aren't loaded yet.
		if (!Resizing.load())
		{
			SafeUpdateHooks(bIsProcessEventHooked, bIsPlayerStateHooked, bIsCameraManagerHooked);
		}
		
		// If a resize happened, wait a bit for it to settle
		if (Resizing.load()) 
		{
			Sleep(500);
		}

		Sleep(200);
	}
	
	Cleanup(hModule);
	return 0;
}
