#include "pch.h"
#include "Utils/AntiDebug.h"

extern HWND g_hWnd;
extern WNDPROC oWndProc;
extern std::atomic<bool> Resizing;
extern std::atomic<bool> Cleaning;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params);

namespace
{
	using NativeCameraUpdateFn = char(__fastcall*)(__int64, __int64*, float);

	constexpr uintptr_t kNativeCameraUpdateRva = 0x3C7C2BE;
	NativeCameraUpdateFn oNativeCameraUpdate = nullptr;
	bool g_NativeCameraUpdateHookInstalled = false;

	std::string DescribeNativeBehaviorList(uintptr_t modePtr)
	{
		if (!modePtr)
			return "Mode=null";

		uintptr_t behaviorsPtr = 0;
		int32_t behaviorCount = 0;
		if (!NativeInterop::ReadPointerNoexcept(modePtr + 48, behaviorsPtr) ||
			!NativeInterop::ReadInt32Noexcept(modePtr + 56, behaviorCount) ||
			behaviorCount <= 0 ||
			!behaviorsPtr)
		{
			return "Behaviors=0";
		}

		std::ostringstream oss;
		oss << "Behaviors=" << behaviorCount << " [";
		const int32_t limit = (std::min)(behaviorCount, 10);
		for (int32_t i = 0; i < limit; ++i)
		{
			uintptr_t behaviorObj = 0;
			if (!NativeInterop::ReadPointerNoexcept(behaviorsPtr + (static_cast<uintptr_t>(i) * 16), behaviorObj) || !behaviorObj)
			{
				if (i != 0) oss << ", ";
				oss << "null";
				continue;
			}

			if (i != 0) oss << ", ";
			oss << "0x" << std::hex << behaviorObj << std::dec;
		}
		if (behaviorCount > limit)
			oss << ", ...";
		oss << "]";
		return oss.str();
	}

	void LogNativeCameraState(const char* phase, __int64 a1)
	{
		if (!Cheats::ShouldTraceNativeCamera())
			return;

		uintptr_t modePtr = 0;
		NativeInterop::ReadPointerNoexcept(static_cast<uintptr_t>(a1) + 14920, modePtr);

		double locX = 0.0, locY = 0.0, locZ = 0.0;
		double rotPitch = 0.0, rotYaw = 0.0, rotRoll = 0.0;
		float fov = 0.0f;
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14640, locX);
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14648, locY);
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14656, locZ);
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14664, rotPitch);
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14672, rotYaw);
		NativeInterop::ReadDoubleNoexcept(static_cast<uintptr_t>(a1) + 14680, rotRoll);
		NativeInterop::ReadFloatNoexcept(static_cast<uintptr_t>(a1) + 14688, fov);

		const std::string category = (std::strcmp(phase, "PreUpdate") == 0) ? "CamNativePre" : "CamNativePost";
		Logger::LogThrottled(
			Logger::Level::Debug,
			category,
			1000,
			"%s Ctx=%p Mode=%p Loc=(%.2f, %.2f, %.2f) Rot=(%.2f, %.2f, %.2f) FOV=%.2f %s",
			phase,
			reinterpret_cast<void*>(a1),
			reinterpret_cast<void*>(modePtr),
			locX, locY, locZ,
			rotPitch, rotYaw, rotRoll,
			fov,
			DescribeNativeBehaviorList(modePtr).c_str());
	}

	char __fastcall hkNativeCameraUpdate(__int64 a1, __int64* a2, float a3)
	{
		const bool bShouldLog = Cheats::ShouldTraceNativeCamera();
		if (bShouldLog)
		{
			LogNativeCameraState("PreUpdate", a1);
		}

		const char result = oNativeCameraUpdate ? oNativeCameraUpdate(a1, a2, a3) : 0;
		Cheats::ApplyNativeCameraPostUpdate(static_cast<uintptr_t>(a1), a3);
		if (bShouldLog)
		{
			LogNativeCameraState("PostUpdate", a1);
		}
		return result;
	}

	bool InstallNativeCameraUpdateHook()
	{
		if (g_NativeCameraUpdateHookInstalled)
			return true;

		const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		if (!imageBase)
			return false;

		void* target = reinterpret_cast<void*>(imageBase + kNativeCameraUpdateRva);
		MH_STATUS createStatus = MH_CreateHook(target, &hkNativeCameraUpdate, reinterpret_cast<LPVOID*>(&oNativeCameraUpdate));
		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			LOG_WARN("CamNative", "MH_CreateHook failed for native camera update: %d", static_cast<int>(createStatus));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(target);
		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			LOG_WARN("CamNative", "MH_EnableHook failed for native camera update: %d", static_cast<int>(enableStatus));
			return false;
		}

		g_NativeCameraUpdateHookInstalled = true;
		LOG_DEBUG("CamNative", "SUCCESS: Native camera update hook installed at RVA 0x%llX", static_cast<unsigned long long>(kNativeCameraUpdateRva));
		return true;
	}
}

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

	FreeConsole();

	HWND hConsole = GetConsoleWindow();
	if (hConsole) PostMessage(hConsole, WM_CLOSE, 0, 0);

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

	if (Utils::bIsInGame && GVars.World && GVars.Character && GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
	{
		InstallNativeCameraUpdateHook();
	}

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
