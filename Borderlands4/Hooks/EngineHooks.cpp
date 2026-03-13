#include "pch.h"

extern HWND g_hWnd;
extern WNDPROC oWndProc;
extern std::atomic<bool> Resizing;
extern std::atomic<bool> Cleaning;
extern FILE* g_ConsoleOut;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params);

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (Cleaning.load())
		return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);

	if (uMsg == WM_SIZE && wParam != SIZE_MINIMIZED)
	{
		Resizing.store(true);
	}

	if (GUI::ShowMenu && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void Cleanup(HMODULE hModule)
{
	Cleaning.store(true);
	
	// Restore WndProc
	if (g_hWnd && oWndProc) {
		SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
	}
	
	Hooks::UnhookAll();
	MH_Uninitialize();
	
	Logger::Shutdown();

	if (g_ConsoleOut) fclose(g_ConsoleOut);
	FreeConsole();

	HWND hConsole = GetConsoleWindow();
	if (hConsole) PostMessage(hConsole, WM_CLOSE, 0, 0);

	FreeLibraryAndExitThread(hModule, 0);
}

// Wrapper for SEH to avoid C2712
static void InternalUpdateHooksSEH(bool& bIsProcessEventHooked, bool& bIsPlayerStateHooked, bool& bIsCameraManagerHooked)
{
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
				if (currentPSVTable && currentPSVTable != Hooks::psVTable)
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
				if (currentCMVTable && currentCMVTable != Hooks::cmVTable)
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
							
							Hooks::psVTable = psVTable; 
							bIsPlayerStateHooked = true;
							// LOG_INFO moved outside because SEH can't mix with RAII
						}
					}
					else {
						Hooks::psVTable = psVTable;
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
							
							Hooks::cmVTable = cmVTable; 
							bIsCameraManagerHooked = true;
						}
					}
					else {
						Hooks::cmVTable = cmVTable;
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

	// Log success outside SEH to allow using std::string conversion in LOG_INFO
	if (!prevPS && bIsPlayerStateHooked) LOG_INFO("Hook", "SUCCESS: PlayerState ProcessEvent Hooked!");
	if (!prevCM && bIsCameraManagerHooked) LOG_INFO("Hook", "SUCCESS: CameraManager ProcessEvent Hooked!");
}

DWORD MainThread(HMODULE hModule)
{
	AllocConsole();
	freopen_s(&g_ConsoleOut, "CONOUT$", "w", stdout);

    Logger::Initialize();
	LOG_INFO("System", "Cheat Injecting...");

	MH_STATUS Status = MH_Initialize();
	if (Status != MH_OK)
	{
		LOG_ERROR("MinHook", "MinHook failed to init: %d", (int)Status);
		Cleanup(hModule);
	}

	Sleep(1000); 

	if (!Engine::HookPresent())
	{
		LOG_ERROR("System", "Failed to initialize DX12 hooks.");
		Cleanup(hModule);
	}
	else
		LOG_INFO("System", "Engine hooks initialized successfully.");

	Sleep(1000); 

	LOG_INFO("System", "Loading configurations...");
	Localization::Initialize();
    HotkeyManager::Initialize();

	ConfigManager::LoadSettings();

	bool bIsProcessEventHooked = false;
	bool bIsPlayerStateHooked = false;
	bool bIsCameraManagerHooked = false;

	while (!Cleaning.load())
	{
		if (!Utils::bIsLoading && !Resizing.load())
		{
			SafeUpdateHooks(bIsProcessEventHooked, bIsPlayerStateHooked, bIsCameraManagerHooked);
		}
		
		if (Resizing.load()) 
		{
			Sleep(100);
			Resizing.store(false); 
		}

		Sleep(100);
	}
	
	Cleanup(hModule);
	return 0;
}
