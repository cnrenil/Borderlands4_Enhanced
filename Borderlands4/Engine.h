#pragma once
#include "pch.h"

struct Engine
{
	static bool HookPresent();
	static bool HookResizeBuffers();
	static bool InitImGui(HWND hwnd);
	
	static DXGI_SWAP_CHAIN_DESC sd;
    static int FrameCount;
};

struct Hooks
{
	using PostRenderFn = void(*)(UGameViewportClient*, UCanvas*);

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

extern void(*oProcessEvent)(const UObject*, UFunction*, void*);
void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params);
