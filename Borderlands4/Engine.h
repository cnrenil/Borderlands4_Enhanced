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
	struct State
	{
		void** pcVTable = nullptr;
		void** psVTable = nullptr;
		void** cmVTable = nullptr;
		void** viewportVTable = nullptr;
	};

	static State& GetState();

	static bool HookProcessEvent();
	static void UnhookAll();
};

extern void(*oProcessEvent)(const UObject*, UFunction*, void*);
extern void(*oPostRender)(UObject*, class UCanvas*);
void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params);
void hkPostRender(UObject* ViewportClient, class UCanvas* Canvas);
