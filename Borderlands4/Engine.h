#pragma once
#include "pch.h"

using namespace SDK;

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
	static void** pcVTable;
	static void** psVTable;
	static void** cmVTable;
	static void** viewportVTable;

	static bool HookProcessEvent();
	static void UnhookAll();
};

extern void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*);
extern void(*oPostRender)(SDK::UObject*, class SDK::UCanvas*);
void hkProcessEvent(const SDK::UObject* Object, SDK::UFunction* Function, void* Params);
void hkPostRender(SDK::UObject* ViewportClient, class SDK::UCanvas* Canvas);
