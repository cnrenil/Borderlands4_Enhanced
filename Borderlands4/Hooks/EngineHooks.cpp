#include "pch.h"
#include "GUI/Menu.h"
#include "Config/ConfigManager.h"
#include "Engine.h"
#include "D3D12Hook.h"
#include "Utils/Localization.h"

extern bool init;
extern int Frames;
extern bool SettingsLoaded;
extern bool menu_key_pressed;
extern std::atomic<int> g_PresentCount;
extern std::atomic<int> g_WndProcCount;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<bool> Cleaning;
extern std::atomic<bool> Resizing;
extern WNDPROC oWndProc;
extern HWND g_hWnd;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	g_WndProcCount.fetch_add(1);

	if (!Cleaning.load())
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
			return true;

		if (GUI::ShowMenu) {
			switch (uMsg) {
				case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
				case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_CHAR:
				case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
					return true;
			}
		}

		if (uMsg == WM_KEYUP && wParam == VK_END) {
			Cleaning.store(true);
			return TRUE;
		}
	}

	LRESULT result = CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
	g_WndProcCount.fetch_sub(1);
	return result;
}

// DX12 rendering logic moved to D3D12Hook.cpp
// Logic for cleaning up and threads remains here for now

static FILE* g_ConsoleOut = nullptr;

static void Cleanup(HMODULE hModule)
{
	Cleaning.store(true);
	ConfigManager::SaveSettings();
	
	Hooks::UnhookAll();

	if (oWndProc && g_hWnd) {
		SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
	}

	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();

	int timeout = 200;
	while ((g_PresentCount.load() != 0 || g_WndProcCount.load() != 0 || g_ProcessEventCount.load() != 0) && timeout-- > 0)
		Sleep(50);

	d3d12hook::release();

	std::cout << "Cleaning up...\n";
	
	if (g_ConsoleOut) fclose(g_ConsoleOut);
	
	HWND hConsole = GetConsoleWindow();
	FreeConsole();
	if (hConsole) PostMessage(hConsole, WM_CLOSE, 0, 0);

	FreeLibraryAndExitThread(hModule, 0);
}

DWORD MainThread(HMODULE hModule)
{
	AllocConsole();
	freopen_s(&g_ConsoleOut, "CONOUT$", "w", stdout);

	Localization::Initialize();

	std::cout << "Cheat Injecting...\n";

	MH_STATUS Status = MH_Initialize();
	if (Status != MH_OK)
	{
		printf("[ERROR] MinHook failed to init: %d", Status);
		Cleanup(hModule);
	}

	Sleep(1000); 

	if (!Engine::HookPresent())
	{
		printf("[ERROR] Failed to initialize hooks.\n");
		Cleanup(hModule);
	}
	else
		printf("Engine hooks initialized successfully.\n");

	Sleep(1000); 

	std::cout << "Cheat Injected\n";

	ConfigManager::LoadSettings();

	bool bIsProcessEventHooked = false;
	bool bIsPlayerStateHooked = false;

	while (!Cleaning.load())
	{
		if (!Utils::bIsLoading)
		{
			if (!bIsProcessEventHooked && GVars.PlayerController)
		{
			bIsProcessEventHooked = Hooks::HookProcessEvent();
		}

		// Re-hook check (incase of level change or respawn)
		// We use a __try block here because GVars pointers can become invalid across threads during level transitions
		__try {
			if (bIsPlayerStateHooked)
			{
				if (GVars.Character && GVars.Character->PlayerState)
				{
					void** currentPSVTable = *reinterpret_cast<void***>(GVars.Character->PlayerState);
					if (currentPSVTable && currentPSVTable != Hooks::psVTable)
					{
						// New PlayerState instance/VTable, need to re-hook
						bIsPlayerStateHooked = false; 
					}
				}
				else
				{
					// Character or PlayerState gone, reset toggle so we look for it again
					bIsPlayerStateHooked = false;
				}
			}
			
			// Only attempt PS hook if PC hook is done (ensures oProcessEvent is valid)
			if (bIsProcessEventHooked && !bIsPlayerStateHooked && GVars.Character && GVars.Character->PlayerState)
			{
				void** psVTable = *reinterpret_cast<void***>(GVars.Character->PlayerState);
				// Basic sanity check on vtable
				if (psVTable && !IsBadReadPtr(psVTable, sizeof(void*) * 80)) 
				{
					if (psVTable[73] != &hkProcessEvent) {
						DWORD old;
						if (VirtualProtect(&psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
							psVTable[73] = (void*)hkProcessEvent;
							VirtualProtect(&psVTable[73], sizeof(void*), old, &old);
							
							Hooks::psVTable = psVTable; // Store for cleanup
							printf("[Hook] SUCCESS: PlayerState ProcessEvent Hooked!\n");
							bIsPlayerStateHooked = true;
						}
					}
					else {
						// Already hooked by us (perhaps from previous instance)
						Hooks::psVTable = psVTable;
						bIsPlayerStateHooked = true;
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			// If we crash here, just reset state and try again later
			bIsPlayerStateHooked = false;
		}
		}
		Sleep(100);
	}
	
	Cleanup(hModule);
	return 0;
}
