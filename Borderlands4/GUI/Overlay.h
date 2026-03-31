#pragma once

namespace GUI::Overlay
{
    bool Initialize(HWND hwnd, ID3D12Device* device, UINT bufferCount, DXGI_FORMAT format, ID3D12DescriptorHeap* srvHeap);
    void Shutdown();
    void BeginFrame();
    void BuildFrame();
    ImDrawData* GetDrawData();
    ImDrawData* BuildFrameAndGetDrawData();
    LRESULT HandleWndProcMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool IsInitialized();
    float GetDpiScale();
}
