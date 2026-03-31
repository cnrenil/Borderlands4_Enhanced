#include "pch.h"
#include "Fonts/Embedded/EmbeddedFontMain.h"
#include "Fonts/Embedded/EmbeddedFontFallback.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GUI::Overlay
{
    namespace
    {
        bool gInitialized = false;
        std::recursive_mutex g_ImGuiMutex;

        void LoadFonts(ImGuiIO& io)
        {
            char winDir[MAX_PATH];
            GetWindowsDirectoryA(winDir, MAX_PATH);
            const std::string preferredFontPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
            const std::string chineseFontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";
            const float primarySize = 21.0f;
            const float mergeSize = 20.0f;

            io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

            ImFontConfig primaryFontConfig{};
            primaryFontConfig.OversampleH = 2;
            primaryFontConfig.OversampleV = 2;
            primaryFontConfig.PixelSnapH = false;
            primaryFontConfig.RasterizerMultiply = 1.0f;
            primaryFontConfig.FontDataOwnedByAtlas = false;

            bool fontLoaded = false;
            if (g_EmbeddedFontMain_size > 0)
            {
                fontLoaded = io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(g_EmbeddedFontMain),
                    static_cast<int>(g_EmbeddedFontMain_size),
                    primarySize,
                    &primaryFontConfig,
                    io.Fonts->GetGlyphRangesDefault()) != nullptr;
            }
            if (!fontLoaded && GetFileAttributesA(preferredFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                fontLoaded = io.Fonts->AddFontFromFileTTF(preferredFontPath.c_str(), primarySize, &primaryFontConfig, io.Fonts->GetGlyphRangesDefault()) != nullptr;
            }
            if (!fontLoaded && GetFileAttributesA(chineseFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                fontLoaded = io.Fonts->AddFontFromFileTTF(chineseFontPath.c_str(), primarySize, &primaryFontConfig, io.Fonts->GetGlyphRangesChineseFull()) != nullptr;
            }
            if (fontLoaded && GetFileAttributesA(chineseFontPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                ImFontConfig mergeFontConfig{};
                mergeFontConfig.MergeMode = true;
                mergeFontConfig.OversampleH = 2;
                mergeFontConfig.OversampleV = 2;
                mergeFontConfig.PixelSnapH = false;
                mergeFontConfig.RasterizerMultiply = 1.0f;
                mergeFontConfig.FontDataOwnedByAtlas = false;
                if (g_EmbeddedFontFallback_size > 0)
                {
                    io.Fonts->AddFontFromMemoryTTF(
                        const_cast<unsigned char*>(g_EmbeddedFontFallback),
                        static_cast<int>(g_EmbeddedFontFallback_size),
                        mergeSize,
                        &mergeFontConfig,
                        io.Fonts->GetGlyphRangesChineseFull());
                }
                else
                {
                    io.Fonts->AddFontFromFileTTF(chineseFontPath.c_str(), mergeSize, &mergeFontConfig, io.Fonts->GetGlyphRangesChineseFull());
                }
            }
            if (!fontLoaded)
            {
                io.Fonts->AddFontDefault();
            }
            io.Fonts->Build();
        }

        void ApplyStableStyle()
        {
            GUI::ThemeManager::ApplyByIndex(ConfigManager::I("Misc.Theme"));
            ImGuiStyle& style = ImGui::GetStyle();
            style.AntiAliasedFill = true;
            style.AntiAliasedLines = true;
            style.AntiAliasedLinesUseTex = true;
            style.CurveTessellationTol = 0.8f;
            style.CircleTessellationMaxError = 0.18f;
        }
    }

    bool Initialize(HWND hwnd, ID3D12Device* device, UINT bufferCount, DXGI_FORMAT format, ID3D12DescriptorHeap* srvHeap)
    {
        std::scoped_lock lock(g_ImGuiMutex);
        if (gInitialized) return true;
        if (!hwnd || !device || !srvHeap || bufferCount == 0) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = false;
        io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
        io.FontGlobalScale = 1.0f;

        LoadFonts(io);
        ApplyStableStyle();

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
        std::scoped_lock lock(g_ImGuiMutex);
        if (!gInitialized) return;
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        gInitialized = false;
    }

    void BeginFrame()
    {
        std::scoped_lock lock(g_ImGuiMutex);
        if (!gInitialized) return;
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = GUI::ShowMenu;
    }

    void BuildFrame()
    {
        std::scoped_lock lock(g_ImGuiMutex);
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
        std::scoped_lock lock(g_ImGuiMutex);
        return gInitialized ? ImGui::GetDrawData() : nullptr;
    }

    ImDrawData* BuildFrameAndGetDrawData()
    {
        std::scoped_lock lock(g_ImGuiMutex);
        if (!gInitialized)
            return nullptr;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = GUI::ShowMenu;

        try
        {
            Cheats::Render();
        }
        catch (...)
        {
        }

        ImGui::Render();
        return ImGui::GetDrawData();
    }

    LRESULT HandleWndProcMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        std::scoped_lock lock(g_ImGuiMutex);
        if (!gInitialized || !ImGui::GetCurrentContext())
            return 0;

        return ::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }

    bool IsInitialized()
    {
        std::scoped_lock lock(g_ImGuiMutex);
        return gInitialized;
    }

    float GetDpiScale()
    {
        return 1.0f;
    }
}
