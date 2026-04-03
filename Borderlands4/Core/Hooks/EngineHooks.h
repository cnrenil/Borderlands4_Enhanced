#pragma once
#include <windows.h>

namespace SDK
{
	class UObject;
	class UFunction;
	class UGameViewportClient;
	class UCanvas;
}

bool HookPresent();

struct Hooks
{
	using PostRenderFn = void(*)(SDK::UGameViewportClient*, SDK::UCanvas*);

	struct State
	{
		void** pcVTable = nullptr;
		void** psVTable = nullptr;
		void** viewportVTable = nullptr;
		PostRenderFn originalPostRender = nullptr;
	};

	static State& GetState();

	static bool HookProcessEvent();
	static bool HookPostRender();
	static bool IsPostRenderHooked();
	static void UnhookAll();
};

extern void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*);
void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params);

DWORD MainThread(HMODULE hModule);
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
