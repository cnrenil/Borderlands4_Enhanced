#include "pch.h"

namespace GUI::Overlay
{
    namespace
    {
        bool gInitialized = false;

        void LoadFonts(ImGuiIO& io)
        {
            char winDir[MAX_PATH];
            GetWindowsDirectoryA(winDir, MAX_PATH);
            const std::string preferredFontPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
            const std::string chineseFontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";

            ImFontConfig primaryFontConfig{};
            primaryFontConfig.OversampleH = 2;
            primaryFontConfig.OversampleV = 2;
            primaryFontConfig.PixelSnapH = false;

            bool fontLoaded = false;
            if (GetFileAttributesA(preferredFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                fontLoaded = io.Fonts->AddFontFromFileTTF(preferredFontPath.c_str(), 19.0f, &primaryFontConfig, io.Fonts->GetGlyphRangesDefault()) != nullptr;
            }
            if (!fontLoaded && GetFileAttributesA(chineseFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                fontLoaded = io.Fonts->AddFontFromFileTTF(chineseFontPath.c_str(), 19.0f, &primaryFontConfig, io.Fonts->GetGlyphRangesChineseFull()) != nullptr;
            }
            if (fontLoaded && GetFileAttributesA(chineseFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ImFontConfig mergeFontConfig{};
                mergeFontConfig.MergeMode = true;
                mergeFontConfig.OversampleH = 2;
                mergeFontConfig.OversampleV = 2;
                mergeFontConfig.PixelSnapH = false;
                io.Fonts->AddFontFromFileTTF(chineseFontPath.c_str(), 18.0f, &mergeFontConfig, io.Fonts->GetGlyphRangesChineseFull());
            }
            if (!fontLoaded)
            {
                io.Fonts->AddFontDefault();
            }
            io.Fonts->Build();
        }
    }

    bool Initialize(HWND hwnd, ID3D12Device* device, UINT bufferCount, DXGI_FORMAT format, ID3D12DescriptorHeap* srvHeap)
    {
        if (gInitialized) return true;
        if (!hwnd || !device || !srvHeap || bufferCount == 0) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = false;

        LoadFonts(io);
        GUI::ThemeManager::ApplyByIndex(ConfigManager::I("Misc.Theme"));

        if (!ImGui_ImplWin32_Init(hwnd))
            return false;

        if (!ImGui_ImplDX12_Init(
                device,
                bufferCount,
                format,
                srvHeap,
                srvHeap->GetCPUDescriptorHandleForHeapStart(),
                srvHeap->GetGPUDescriptorHandleForHeapStart()))
        {
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        gInitialized = true;
        return true;
    }

    void Shutdown()
    {
        if (!gInitialized) return;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        gInitialized = false;
    }

    void BeginFrame()
    {
        if (!gInitialized) return;
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = GUI::ShowMenu;
    }

    void BuildFrame()
    {
        if (!gInitialized) return;
        try
        {
            Cheats::Render();
        }
        catch (...)
        {
        }
        ImGui::Render();
    }

    ImDrawData* GetDrawData()
    {
        return gInitialized ? ImGui::GetDrawData() : nullptr;
    }

    bool IsInitialized()
    {
        return gInitialized;
    }
}
