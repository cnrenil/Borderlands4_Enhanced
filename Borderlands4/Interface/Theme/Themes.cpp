#include "pch.h"

namespace
{
    std::vector<GUI::ThemeDefinition> g_Themes;
    bool g_ThemesInitialized = false;

    void ApplyBaseLayout(ImGuiStyle& style)
    {
        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.55f;
        style.WindowPadding = ImVec2(20.0f, 18.0f);
        style.WindowRounding = 24.0f;
        style.WindowBorderSize = 0.0f;
        style.WindowMinSize = ImVec2(300.0f, 220.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.ChildRounding = 20.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupRounding = 18.0f;
        style.PopupBorderSize = 0.0f;
        style.FramePadding = ImVec2(12.0f, 10.0f);
        style.FrameRounding = 16.0f;
        style.FrameBorderSize = 0.0f;
        style.ItemSpacing = ImVec2(12.0f, 11.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 7.0f);
        style.CellPadding = ImVec2(8.0f, 6.0f);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 12.0f;
        style.ScrollbarRounding = 12.0f;
        style.GrabMinSize = 10.0f;
        style.GrabRounding = 12.0f;
        style.TabRounding = 16.0f;
        style.TabBorderSize = 0.0f;
        style.TabBarBorderSize = 0.0f;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextBorderSize = 1.0f;
        style.SeparatorTextPadding = ImVec2(6.0f, 10.0f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
    }

    void ApplyOceanGlass(ImGuiStyle& style)
    {
        ImGui::StyleColorsDark(&style);
        ApplyBaseLayout(style);
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.96f, 0.97f, 0.99f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.60f, 0.67f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
        colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.035f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.10f, 0.16f, 0.74f);
        colors[ImGuiCol_Border] = ImVec4(0.72f, 0.84f, 0.92f, 0.16f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.045f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.065f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.03f, 0.05f, 0.08f, 0.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.03f, 0.05f, 0.08f, 0.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.03f, 0.05f, 0.08f, 0.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.11f, 0.15f, 0.42f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.07f, 0.10f, 0.18f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.62f, 0.72f, 0.34f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.54f, 0.74f, 0.85f, 0.50f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.65f, 0.86f, 0.98f, 0.62f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.90f, 0.84f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.43f, 0.72f, 0.84f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.63f, 0.88f, 0.98f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
        colors[ImGuiCol_Header] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.045f);
        colors[ImGuiCol_HeaderActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.065f);
        colors[ImGuiCol_Separator] = ImVec4(0.54f, 0.70f, 0.80f, 0.18f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.58f, 0.68f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.53f, 0.76f, 0.88f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.45f, 0.54f, 0.16f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.65f, 0.78f, 0.30f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.58f, 0.83f, 0.97f, 0.44f);
        colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.14f, 0.18f, 0.95f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.21f, 0.46f, 0.56f, 0.95f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.36f, 0.44f, 1.00f);
        colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.74f, 0.92f, 0.86f, 1.00f);
        colors[ImGuiCol_TabDimmed] = ImVec4(0.08f, 0.10f, 0.14f, 0.88f);
        colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.13f, 0.24f, 0.30f, 1.00f);
        colors[ImGuiCol_PlotLines] = ImVec4(0.48f, 0.74f, 0.84f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.80f, 0.93f, 0.98f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.74f, 0.63f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.72f, 0.95f, 0.84f, 1.00f);
        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.11f, 0.14f, 0.18f, 0.36f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.52f, 0.68f, 0.78f, 0.20f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.30f, 0.42f, 0.50f, 0.12f);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.13f, 0.17f, 0.12f);
        colors[ImGuiCol_TextLink] = ImVec4(0.64f, 0.87f, 0.96f, 1.00f);
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.53f, 0.67f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.75f, 0.92f, 0.84f, 1.00f);
        colors[ImGuiCol_NavCursor] = ImVec4(0.77f, 0.95f, 0.90f, 1.00f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.36f);
    }

}

void GUI::ThemeManager::Register(const std::string& id, const Localization::LocalizedText& displayName, std::function<void(ImGuiStyle&)> apply)
{
    if (id.empty() || !apply)
        return;

    for (const ThemeDefinition& theme : g_Themes)
    {
        if (theme.Id == id)
            return;
    }

    g_Themes.push_back({ id, displayName, std::move(apply) });
}

void GUI::ThemeManager::Initialize()
{
    if (g_ThemesInitialized)
        return;

    g_Themes.clear();
    Register("theme_studio", Localization::LocalizedText::Make({
        { Language::English, "Theme Studio" },
        { Language::Chinese, (const char*)u8"主题工坊" }
    }), ApplyOceanGlass);
    g_ThemesInitialized = true;
}

int GUI::ThemeManager::ClampThemeIndex(int index)
{
    Initialize();
    if (g_Themes.empty())
        return 0;
    return (std::clamp)(index, 0, static_cast<int>(g_Themes.size()) - 1);
}

const GUI::ThemeDefinition* GUI::ThemeManager::GetThemeByIndex(int index)
{
    Initialize();
    if (g_Themes.empty())
        return nullptr;

    const int clampedIndex = ClampThemeIndex(index);
    return &g_Themes[clampedIndex];
}

const char* GUI::ThemeManager::GetThemeDisplayName(int index)
{
    const ThemeDefinition* theme = GetThemeByIndex(index);
    if (!theme)
        return "";

    return theme->DisplayName.Resolve();
}

int GUI::ThemeManager::GetThemeCount()
{
    Initialize();
    return static_cast<int>(g_Themes.size());
}

void GUI::ThemeManager::ApplyByIndex(int index)
{
    const ThemeDefinition* theme = GetThemeByIndex(index);
    if (!theme)
        return;

    ImGuiStyle& style = ImGui::GetStyle();
    theme->Apply(style);
    ApplyRuntimeAccent();
}

void GUI::ThemeManager::ApplyRuntimeAccent()
{
    if (!ConfigManager::Exists("Misc.ThemeAccent"))
        return;

    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
    const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
    const ImVec4 tint = ConfigManager::Color("Misc.ThemeTint");
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = glow;
    colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    colors[ImGuiCol_ButtonActive] = ImVec4(glow.x, glow.y, glow.z, 0.42f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.26f);
    colors[ImGuiCol_HeaderActive] = ImVec4(glow.x, glow.y, glow.z, 0.34f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(glow.x, glow.y, glow.z, 0.54f);
    colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
    colors[ImGuiCol_TabSelected] = ImVec4(accent.x * 0.80f, accent.y * 0.80f, accent.z * 0.80f, 0.88f);
    colors[ImGuiCol_TabSelectedOverline] = glow;
    colors[ImGuiCol_TextLink] = glow;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.26f);
    colors[ImGuiCol_NavCursor] = glow;
    colors[ImGuiCol_WindowBg] = ImVec4(tint.x, tint.y, tint.z, tint.w * 0.16f);
    colors[ImGuiCol_ChildBg] = ImVec4(tint.x, tint.y, tint.z, tint.w * 0.12f);
}
