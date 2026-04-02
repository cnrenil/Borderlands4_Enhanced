#pragma once

namespace GUI
{
    struct MenuBackdropState
    {
        bool Visible = false;
        ImVec2 Pos{};
        ImVec2 Size{};
        float Rounding = 28.0f;
        float Opacity = 0.88f;
        ImVec4 Tint = ImVec4(0.08f, 0.10f, 0.14f, 0.52f);
        ImVec4 Accent = ImVec4(0.33f, 0.72f, 0.93f, 1.0f);
        ImVec4 Glow = ImVec4(0.66f, 0.90f, 1.0f, 1.0f);
        float GlowStrength = 0.42f;
        float GlowSpread = 22.0f;
    };

    const MenuBackdropState& GetMenuBackdropState();
}

namespace GUI::BackdropBlur
{
    struct GlowRect
    {
        ImVec2 Min{};
        ImVec2 Max{};
        float Rounding = 0.0f;
        ImVec4 Color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        float StrengthScale = 1.0f;
        float SpreadScale = 1.0f;
    };

    bool Initialize(ID3D12Device* device, ID3D12DescriptorHeap* sharedSrvHeap, DXGI_FORMAT format, UINT width, UINT height);
    void Shutdown();
    void Resize(UINT width, UINT height, DXGI_FORMAT format);
    void BeginGlowFrame();
    void SubmitGlowRect(const GlowRect& rect);
    void Render(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* backBuffer,
        const D3D12_CPU_DESCRIPTOR_HANDLE& backBufferRtv,
        const MenuBackdropState& backdropState);
}
