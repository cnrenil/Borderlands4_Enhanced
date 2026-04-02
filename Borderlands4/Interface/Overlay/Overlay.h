#pragma once

namespace GUI::Overlay
{
    bool Initialize(HWND hwnd, ID3D12Device* device, UINT bufferCount, DXGI_FORMAT format, ID3D12DescriptorHeap* srvHeap);
    void Shutdown();

    // Bracket a render frame:
    //   BeginFrame()             — ImGui NewFrame setup
    //   Scheduler::OnRenderFrame() — all registered render callbacks (called by D3D12Hook)
    //   EndFrame()               — ImGui::Render() + returns GetDrawData()
    void BeginFrame();
    ImDrawData* EndFrame();

    LRESULT HandleWndProcMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool IsInitialized();
    float GetDpiScale();
}
