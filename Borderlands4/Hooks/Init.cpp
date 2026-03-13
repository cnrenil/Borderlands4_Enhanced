#include "pch.h"


DXGI_SWAP_CHAIN_DESC Engine::sd = {};
int Engine::FrameCount = 3;

bool HookPresentLocal();

bool Engine::HookPresent()
{
	if (!HookPresentLocal())
		return false;

	return true;
}

bool HookPresentLocal()
{
	WNDCLASSA wc = { 0 };
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "DummyWindowClass";
	RegisterClassA(&wc);

	HWND dummyWnd = CreateWindowA(wc.lpszClassName, "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
	if (!dummyWnd) return false;

	HMODULE hD3D12 = GetModuleHandleA("d3d12.dll");
	HMODULE hDXGI = GetModuleHandleA("dxgi.dll");
	if (hD3D12 && hDXGI) {
		auto pfnCreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(hD3D12, "D3D12CreateDevice");
		auto pfnCreateDXGIFactory = (HRESULT(WINAPI*)(REFIID, void**))GetProcAddress(hDXGI, "CreateDXGIFactory");

		if (pfnCreateDevice && pfnCreateDXGIFactory) {
			IDXGIFactory* pFactory = nullptr;
			ID3D12Device* pDummyDevice = nullptr;
			if (SUCCEEDED(pfnCreateDXGIFactory(IID_PPV_ARGS(&pFactory))) &&
				SUCCEEDED(pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDummyDevice)))) {
				
				D3D12_COMMAND_QUEUE_DESC queueDesc = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
				ID3D12CommandQueue* pDummyQueue = nullptr;
				pDummyDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pDummyQueue));

				DXGI_SWAP_CHAIN_DESC desc = {};
				desc.BufferCount = 2;
				desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				desc.OutputWindow = dummyWnd;
				desc.SampleDesc.Count = 1;
				desc.Windowed = TRUE;
				desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

				IDXGISwapChain* pDummySwapChain = nullptr;
				pFactory->CreateSwapChain(pDummyQueue, &desc, &pDummySwapChain);

				if (pDummySwapChain && pDummyQueue) {
					void** pSwapChainVTable = *reinterpret_cast<void***>(pDummySwapChain);
					void** pQueueVTable = *reinterpret_cast<void***>(pDummyQueue);

					// Initialize the D3D12 hook with captured pointers from vTable
					d3d12hook::oPresentD3D12 = (PresentD3D12)pSwapChainVTable[8];
					d3d12hook::oPresent1D3D12 = (Present1Fn)pSwapChainVTable[22];
					d3d12hook::oResizeBuffersD3D12 = (ResizeBuffersFn)pSwapChainVTable[13];
					d3d12hook::oExecuteCommandListsD3D12 = (ExecuteCommandListsFn)pQueueVTable[10];

					MH_CreateHook(pSwapChainVTable[8], (LPVOID)&d3d12hook::hookPresentD3D12, (LPVOID*)&d3d12hook::oPresentD3D12);
					MH_CreateHook(pSwapChainVTable[22], (LPVOID)&d3d12hook::hookPresent1D3D12, (LPVOID*)&d3d12hook::oPresent1D3D12);
					MH_CreateHook(pSwapChainVTable[13], (LPVOID)&d3d12hook::hookResizeBuffersD3D12, (LPVOID*)&d3d12hook::oResizeBuffersD3D12);
					MH_CreateHook(pQueueVTable[10], (LPVOID)&d3d12hook::hookExecuteCommandListsD3D12, (LPVOID*)&d3d12hook::oExecuteCommandListsD3D12);

					MH_EnableHook(MH_ALL_HOOKS);

					pDummySwapChain->Release();
					pDummyQueue->Release();
				}
				pDummyDevice->Release();
				pFactory->Release();
			}
		}
	}
	
	if (dummyWnd) DestroyWindow(dummyWnd);
	return true;
}
