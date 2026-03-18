#include "pch.h"

extern HWND g_hWnd;
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

    // ALWAYS pass to ImGui first so it can track key states for Aimbot/Hotkeys
    // even when the menu is closed.
    ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

	if (GUI::ShowMenu) {
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

		if (bIsCameraManagerHooked)
		{
			if (GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
			{
				void** currentCMVTable = *reinterpret_cast<void***>(GVars.PlayerController->PlayerCameraManager);
				if (currentCMVTable && currentCMVTable != hookState.cmVTable)
				{
					bIsCameraManagerHooked = false;
				}
			}
			else
			{
				bIsCameraManagerHooked = false;
			}
		}

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

			if (!bIsCameraManagerHooked && GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
			{
				void** cmVTable = *reinterpret_cast<void***>(GVars.PlayerController->PlayerCameraManager);
				if (cmVTable && !IsBadReadPtr(cmVTable, sizeof(void*) * 80)) 
				{
					if (cmVTable[73] != &hkProcessEvent) {
						DWORD old;
						if (VirtualProtect(&cmVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
							cmVTable[73] = (void*)hkProcessEvent;
							VirtualProtect(&cmVTable[73], sizeof(void*), old, &old);
							
							hookState.cmVTable = cmVTable; 
							bIsCameraManagerHooked = true;
						}
					}
					else {
						hookState.cmVTable = cmVTable;
						bIsCameraManagerHooked = true;
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
	if (!prevCM && bIsCameraManagerHooked) LOG_DEBUG("Hook", "SUCCESS: CameraManager ProcessEvent Hooked!");

	// Logger::LogThrottled(Logger::Level::Debug, "Hook", 10000, "SafeUpdateHooks: Hooks Status (PE: %d, PS: %d, CM: %d)", bIsProcessEventHooked, bIsPlayerStateHooked, bIsCameraManagerHooked);
}

static void AutoSetVariablesLocked()
{
	SDK::UWorld* currentWorld = Utils::GetWorldSafe();
	bool shouldReleaseShadowCamera = false;
	bool hadShadowCamera = false;
	{
		std::scoped_lock GVarsLock(gGVarsMutex);
		hadShadowCamera = GVars.CameraActor != nullptr;
		shouldReleaseShadowCamera = hadShadowCamera && GVars.World != nullptr && GVars.World != currentWorld;
	}

	if (shouldReleaseShadowCamera)
	{
		Cheats::ShutdownCamera();
	}

	if (!shouldReleaseShadowCamera && hadShadowCamera)
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
			Logger::LogThrottled(Logger::Level::Debug, "Camera", 1000, "MainThread detected world/gameplay teardown before GVars refresh, releasing shadow camera");
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

	while (!Cleaning.load())
	{
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
