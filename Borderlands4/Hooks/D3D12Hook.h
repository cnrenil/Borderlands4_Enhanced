#pragma once
#include "pch.h"

typedef long(__stdcall* PresentD3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
typedef long(__stdcall* Present1Fn)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);
typedef void(__stdcall* ExecuteCommandListsFn)(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

namespace d3d12hook {
    extern PresentD3D12 oPresentD3D12;
    extern Present1Fn oPresent1D3D12;
    extern ExecuteCommandListsFn oExecuteCommandListsD3D12;
    extern ResizeBuffersFn oResizeBuffersD3D12;

    void release();
    
    long __stdcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
    long __stdcall hookPresent1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);
    void __stdcall hookExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
    HRESULT __stdcall hookResizeBuffersD3D12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
}
