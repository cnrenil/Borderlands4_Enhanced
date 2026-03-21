#include "pch.h"
#include "Utils/AntiDebug.h"


#define MAJORVERSION 0
#define MINORVERSION 1
#define PATCHVERSION 0

namespace GUI
{
    bool ShowMenu = true;
    ImGuiKey TriggerBotKey = ImGuiKey_None;
    ImGuiKey ESPKey = ImGuiKey_None;
    ImGuiKey AimButton = ImGuiKey_None;
}

static const std::pair<const char*, std::string> BoneOptions[] = {
	{"Head", CheatsData::BoneList.HeadBone},
	{"Neck", CheatsData::BoneList.NeckBone},
	{"Chest", CheatsData::BoneList.ChestBone},
	{"Stomach", CheatsData::BoneList.StomachBone},
	{"Pelvis", CheatsData::BoneList.PelvisBone}
};

extern std::atomic<int> g_PresentCount;

static const char* GetTargetModeLabel(int mode)
{
	switch (mode)
	{
	case 1:
		return Localization::T("TARGET_MODE_DISTANCE");
	case 0:
	default:
		return Localization::T("TARGET_MODE_SCREEN");
	}
}

namespace
{
    enum class PanelId
    {
        Player,
        Camera,
        Aimbot,
        Weapon,
        Esp,
        World,
        ThemeStudio,
        Misc,
        Hotkeys,
        About,
        Count
    };
    PanelId g_CurrentPanel = PanelId::Player;
    GUI::MenuBackdropState g_MenuBackdropState{};
    bool g_SurfaceContentOpen = false;
    PanelId g_LastAnimatedPanel = PanelId::Player;
    float g_PanelTransition = 1.0f;

    float GetMenuUiScale()
    {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float scaleX = displaySize.x / 1920.0f;
        const float scaleY = displaySize.y / 1080.0f;
        return std::clamp((std::min)(scaleX, scaleY), 0.84f, 1.32f);
    }

    float AnimateValue(ImGuiID id, float target, float speed = 10.0f)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const float current = storage->GetFloat(id, target);
        const float dt = (std::max)(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);
        const float next = current + (target - current) * (1.0f - std::exp(-speed * dt));
        storage->SetFloat(id, next);
        return next;
    }

    float UpdatePanelTransition()
    {
        if (g_LastAnimatedPanel != g_CurrentPanel)
        {
            g_LastAnimatedPanel = g_CurrentPanel;
            g_PanelTransition = 0.0f;
        }

        const float dt = (std::max)(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);
        g_PanelTransition += (1.0f - g_PanelTransition) * (1.0f - std::exp(-10.0f * dt));
        return (std::clamp)(g_PanelTransition, 0.0f, 1.0f);
    }

    ImVec4 SaturateColor(const ImVec4& color, float amount)
    {
        return ImVec4(
            (std::min)(color.x * amount, 1.0f),
            (std::min)(color.y * amount, 1.0f),
            (std::min)(color.z * amount, 1.0f),
            color.w);
    }

    ImU32 ColorToU32(const ImVec4& color, float alphaScale = 1.0f)
    {
        ImVec4 copy = color;
        copy.w *= alphaScale;
        return ImGui::ColorConvertFloat4ToU32(copy);
    }

    void DrawGlowRect(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const ImVec4& color, float rounding, float strength, float thickness = 1.0f)
    {
        (void)drawList;
        (void)thickness;
        GUI::BackdropBlur::SubmitGlowRect({
            min,
            max,
            rounding,
            color,
            strength,
            1.0f
        });
    }

    void SubmitTextGlow(const ImVec2& pos, const ImVec2& size, const ImVec4& color, float strength = 0.30f)
    {
        GUI::BackdropBlur::SubmitGlowRect({
            ImVec2(pos.x - 4.0f, pos.y - 2.0f),
            ImVec2(pos.x + size.x + 4.0f, pos.y + size.y + 2.0f),
            6.0f,
            color,
            strength,
            0.72f
        });
    }

    bool LocalizedColorEditor(const char* label, float* color, ImGuiColorEditFlags flags = 0)
    {
        ImGui::PushID(label);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float previewSize = ImGui::GetFrameHeight() * 1.35f;
        const ImGuiID popupId = ImGui::GetID("##color_popup");
        static std::unordered_map<ImGuiID, ImVec4> backups;
        ImVec4 current(color[0], color[1], color[2], color[3]);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - previewSize - style.FramePadding.x));
        const bool openPopup = ImGui::ColorButton(
            "##color_button",
            current,
            (flags & ~ImGuiColorEditFlags_NoInputs) | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
            ImVec2(previewSize, previewSize));

        bool valueChanged = false;
        if (openPopup)
        {
            backups[popupId] = current;
            ImGui::OpenPopup("##color_popup");
        }

        if (ImGui::BeginPopup("##color_popup"))
        {
            ImVec4& original = backups[popupId];
            valueChanged |= ImGui::ColorPicker4(
                "##picker",
                color,
                (flags | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoOptions),
                &original.x);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted(Localization::T("COLOR_CURRENT"));
            ImGui::ColorButton("##current_preview", ImVec4(color[0], color[1], color[2], color[3]), ImGuiColorEditFlags_NoTooltip, ImVec2(previewSize * 1.6f, previewSize));
            ImGui::SameLine();
            ImGui::TextUnformatted(Localization::T("COLOR_ORIGINAL"));
            ImGui::SameLine();
            if (ImGui::ColorButton("##original_preview", original, ImGuiColorEditFlags_NoTooltip, ImVec2(previewSize * 1.6f, previewSize)))
            {
                color[0] = original.x;
                color[1] = original.y;
                color[2] = original.z;
                color[3] = original.w;
                valueChanged = true;
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        return valueChanged;
    }

    bool AnimatedButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool prominent = false)
    {
        ImGui::PushID(label);
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 buttonSize = size;
        if (buttonSize.x <= 0.0f)
            buttonSize.x = textSize.x + style.FramePadding.x * 2.6f;
        if (buttonSize.y <= 0.0f)
            buttonSize.y = 34.0f * GetMenuUiScale();

        const ImGuiID id = ImGui::GetID("##animated_button");
        const bool pressed = ImGui::InvisibleButton("##animated_button", buttonSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();

        const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        const float hoverT = AnimateValue(id, hovered ? 1.0f : 0.0f, 11.0f);
        const float pressT = AnimateValue(id + 1, held ? 1.0f : 0.0f, 18.0f);
        const float pulse = 0.5f + 0.5f * std::sinf(static_cast<float>(ImGui::GetTime()) * 2.6f + static_cast<float>(id & 255));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        DrawGlowRect(
            drawList,
            ImVec2(rect.Min.x - 1.0f - hoverT, rect.Min.y - 1.0f - hoverT),
            ImVec2(rect.Max.x + 1.0f + hoverT, rect.Max.y + 1.0f + hoverT),
            prominent ? glow : accent,
            style.FrameRounding + 2.0f,
            0.35f + hoverT * 0.55f + (prominent ? 0.20f : 0.0f));
        drawList->AddRectFilled(
            rect.Min,
            rect.Max,
            ColorToU32(prominent ? SaturateColor(accent, 0.72f + hoverT * 0.32f) : ImVec4(1.0f, 1.0f, 1.0f, 0.05f + hoverT * 0.06f)),
            style.FrameRounding);
        drawList->AddRect(
            rect.Min,
            rect.Max,
            ColorToU32(prominent ? glow : ImVec4(accent.x, accent.y, accent.z, 0.30f + hoverT * 0.24f), 0.35f + hoverT * 0.45f),
            style.FrameRounding,
            0,
            1.1f + hoverT * 0.7f);
        if (prominent)
        {
            drawList->AddRectFilled(
                ImVec2(rect.Min.x + 10.0f, rect.Max.y - 4.0f),
                ImVec2(rect.Min.x + rect.GetWidth() * (0.28f + hoverT * (0.42f + pulse * 0.08f)), rect.Max.y - 2.0f),
                ColorToU32(glow, 0.16f + hoverT * 0.14f),
                style.FrameRounding);
        }

        const float textShift = pressT * 1.5f;
        const ImVec2 textPos(rect.Min.x + (rect.GetWidth() - textSize.x) * 0.5f, rect.Min.y + (rect.GetHeight() - textSize.y) * 0.5f + textShift);
        SubmitTextGlow(textPos, textSize, prominent ? glow : accent, 0.24f + hoverT * 0.22f);
        drawList->AddText(
            textPos,
            ColorToU32(prominent ? ImVec4(0.98f, 0.99f, 1.0f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text)),
            label);

        ImGui::PopID();
        return pressed;
    }

    bool AnimatedToggle(const char* label, bool* value)
    {
        ImGui::PushID(label);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float height = 28.0f * GetMenuUiScale();
        const float width = 50.0f * GetMenuUiScale();
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float fullWidth = ImGui::GetContentRegionAvail().x;

        ImGui::InvisibleButton("##toggle", ImVec2(fullWidth, (std::max)(height, labelSize.y + 8.0f)));
        const bool clicked = ImGui::IsItemClicked();
        if (clicked)
            *value = !*value;

        const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImVec2 togglePos(rect.Max.x - width, rect.Min.y + (rect.GetHeight() - height) * 0.5f);
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        const ImGuiID id = ImGui::GetID("##toggle_anim");
        const float onT = AnimateValue(id, *value ? 1.0f : 0.0f, 14.0f);
        const float hoverT = AnimateValue(id + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 textPos(rect.Min.x, rect.Min.y + (rect.GetHeight() - labelSize.y) * 0.5f);
        SubmitTextGlow(textPos, labelSize, glow, 0.18f + hoverT * 0.18f);
        drawList->AddText(
            textPos,
            ColorToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.92f + hoverT * 0.08f),
            label);

        DrawGlowRect(
            drawList,
            ImVec2(togglePos.x - 1.0f, togglePos.y - 1.0f),
            ImVec2(togglePos.x + width + 1.0f, togglePos.y + height + 1.0f),
            *value ? glow : accent,
            height * 0.5f + 2.0f,
            0.24f + onT * 0.55f + hoverT * 0.24f);
        drawList->AddRectFilled(
            togglePos,
            ImVec2(togglePos.x + width, togglePos.y + height),
            ColorToU32(*value ? SaturateColor(accent, 0.65f + hoverT * 0.35f) : ImVec4(1.0f, 1.0f, 1.0f, 0.08f + hoverT * 0.05f)),
            height * 0.5f);
        drawList->AddRect(
            ImVec2(togglePos.x - 1.0f - hoverT, togglePos.y - 1.0f - hoverT),
            ImVec2(togglePos.x + width + 1.0f + hoverT, togglePos.y + height + 1.0f + hoverT),
            ColorToU32(glow, 0.08f + onT * 0.18f + hoverT * 0.12f),
            height * 0.5f + 2.0f,
            0,
            2.0f);
        drawList->AddRect(
            togglePos,
            ImVec2(togglePos.x + width, togglePos.y + height),
            ColorToU32(*value ? glow : ImVec4(accent.x, accent.y, accent.z, 0.22f), 0.30f + hoverT * 0.40f),
            height * 0.5f,
            0,
            1.0f);

        const float knobRadius = height * 0.5f - 3.0f;
        const float knobX = togglePos.x + 3.0f + knobRadius + (width - (knobRadius * 2.0f + 6.0f)) * onT;
        drawList->AddCircleFilled(
            ImVec2(knobX, togglePos.y + height * 0.5f),
            knobRadius,
            ColorToU32(*value ? ImVec4(1.0f, 1.0f, 1.0f, 0.98f) : ImVec4(0.88f, 0.90f, 0.94f, 0.96f)));
        drawList->AddCircle(
            ImVec2(knobX, togglePos.y + height * 0.5f),
            knobRadius + hoverT,
            ColorToU32(glow, 0.10f + onT * 0.18f),
            24,
            2.0f);

        ImGui::PopID();
        return clicked;
    }

    bool SidebarTabButton(PanelId panelId, const char* label)
    {
        ImGui::PushID(static_cast<int>(panelId));
        const bool selected = g_CurrentPanel == panelId;
        const float scale = GetMenuUiScale();
        const float height = 58.0f * scale;
        const bool pressed = ImGui::InvisibleButton("##sidebar_tab", ImVec2(ImGui::GetContentRegionAvail().x, height));
        if (pressed)
            g_CurrentPanel = panelId;

        const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImGuiID id = ImGui::GetID("##sidebar_tab_anim");
        const float selectedT = AnimateValue(id, selected ? 1.0f : 0.0f, 10.0f);
        const float hoverT = AnimateValue(id + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f, 14.0f);
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        DrawGlowRect(
            drawList,
            rect.Min,
            rect.Max,
            selected ? glow : accent,
            18.0f * scale,
            0.20f + selectedT * 0.52f + hoverT * 0.24f);
        drawList->AddRectFilled(
            rect.Min,
            rect.Max,
            ColorToU32(ImVec4(accent.x, accent.y, accent.z, 0.06f + selectedT * 0.22f + hoverT * 0.08f)),
            18.0f * GetMenuUiScale());
        drawList->AddRect(
            rect.Min,
            rect.Max,
            ColorToU32(glow, 0.08f + selectedT * 0.28f + hoverT * 0.12f),
            18.0f * scale,
            0,
            1.0f + selectedT);
        drawList->AddRectFilled(
            ImVec2(rect.Min.x, rect.Min.y + 8.0f),
            ImVec2(rect.Min.x + 4.0f + selectedT * 4.0f, rect.Max.y - 8.0f),
            ColorToU32(glow, 0.22f + selectedT * 0.48f),
            6.0f);
        ImGui::PushFont(ImGui::GetFont());
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 textPos(
            rect.Min.x + (rect.GetWidth() - textSize.x) * 0.5f,
            rect.Min.y + (rect.GetHeight() - textSize.y) * 0.5f - 1.0f);
        SubmitTextGlow(textPos, textSize, selected ? glow : accent, 0.20f + selectedT * 0.24f + hoverT * 0.14f);
        drawList->AddText(
            textPos,
            ColorToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.88f + selectedT * 0.12f),
            label);
        ImGui::PopFont();

        ImGui::PopID();
        return pressed;
    }

    const char* GetPanelTitle(PanelId panel)
    {
        switch (panel)
        {
        case PanelId::Player: return Localization::T("TAB_PLAYER");
        case PanelId::Camera: return Localization::T("CAMERA_SETTINGS");
        case PanelId::Aimbot: return Localization::T("AIMBOT");
        case PanelId::Weapon: return Localization::T("TAB_WEAPON");
        case PanelId::Esp: return Localization::T("ESP_SETTINGS");
        case PanelId::World: return Localization::T("TAB_WORLD");
        case PanelId::ThemeStudio: return Localization::T("THEME_STUDIO");
        case PanelId::Misc: return Localization::T("TAB_CONFIG");
        case PanelId::Hotkeys: return Localization::T("TAB_HOTKEYS");
        case PanelId::About:
        default:
            return Localization::T("TAB_ABOUT");
        }
    }

    void SectionHeader(const char* label);

    bool BeginSurface(const char* id, const char* title)
    {
        g_SurfaceContentOpen = false;
        const float scale = GetMenuUiScale();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26.0f * scale, 20.0f * scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 0.045f));
        const bool open = ImGui::BeginChild(
            id,
            ImVec2(0.0f, 0.0f),
            true,
            ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (open)
        {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
            DrawGlowRect(
                drawList,
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y),
                glow,
                20.0f * scale,
                0.22f,
                1.2f);
            drawList->AddLine(
                ImVec2(pos.x + 20.0f * scale, pos.y + 18.0f * scale),
                ImVec2(pos.x + size.x * 0.42f, pos.y + 18.0f * scale),
                ColorToU32(glow, 0.34f),
                2.4f);
            ImGui::Dummy(ImVec2(6.0f * scale, 0.0f));
            ImGui::SeparatorText(title);
            ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
            ImGui::BeginChild("##surface_content", ImVec2(-16.0f * scale, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::Indent(14.0f * scale);
            g_SurfaceContentOpen = true;
        }
        return open;
    }

    void EndSurface()
    {
        if (g_SurfaceContentOpen)
        {
            ImGui::Unindent(14.0f * GetMenuUiScale());
            ImGui::EndChild();
            g_SurfaceContentOpen = false;
        }
        ImGui::EndChild();
    }

    void RenderChrome(const ImVec2& pos, const ImVec2& size)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        const float rounding = 30.0f * GetMenuUiScale();
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        const float pulse = 0.5f + 0.5f * std::sinf(static_cast<float>(ImGui::GetTime()) * 1.7f);

        drawList->AddRect(
            ImVec2(pos.x - 3.0f, pos.y - 3.0f),
            ImVec2(max.x + 3.0f, max.y + 3.0f),
            ColorToU32(glow, 0.10f + pulse * 0.06f),
            rounding + 3.0f,
            0,
            5.0f);
        drawList->AddRect(pos, max, ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), rounding, 0, 1.0f);
        drawList->AddRect(ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.06f)), rounding - 1.0f, 0, 1.0f);
        drawList->AddLine(ImVec2(pos.x + 22.0f, pos.y + 26.0f), ImVec2(pos.x + size.x * (0.40f + pulse * 0.08f), pos.y + 26.0f), ColorToU32(glow, 0.18f + pulse * 0.14f), 2.0f);
        drawList->AddLine(ImVec2(pos.x + 22.0f, pos.y + 70.0f), ImVec2(max.x - 22.0f, pos.y + 70.0f), ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)), 1.0f);
        drawList->AddRectFilled(ImVec2(pos.x + 22.0f, pos.y + 22.0f), ImVec2(pos.x + 120.0f, pos.y + 26.0f), ColorToU32(accent, 0.75f), 4.0f);
    }

    void RenderSidebar()
    {
        if (!BeginSurface("##sidebar", Localization::T("MENU_TITLE")))
        {
            EndSurface();
            return;
        }

        ImGui::SetWindowFontScale(1.22f);
        ImGui::TextDisabled(Localization::T("VERSION_D_D_D"), MAJORVERSION, MINORVERSION, PATCHVERSION);
        ImGui::Spacing();

        SidebarTabButton(PanelId::Player, Localization::T("TAB_PLAYER"));
        SidebarTabButton(PanelId::Camera, Localization::T("CAMERA_SETTINGS"));
        SidebarTabButton(PanelId::Aimbot, Localization::T("AIMBOT"));
        SidebarTabButton(PanelId::Esp, Localization::T("ESP_SETTINGS"));
        SidebarTabButton(PanelId::Weapon, Localization::T("TAB_WEAPON"));
        SidebarTabButton(PanelId::World, Localization::T("TAB_WORLD"));
        SidebarTabButton(PanelId::ThemeStudio, Localization::T("THEME_STUDIO"));
        SidebarTabButton(PanelId::Misc, Localization::T("TAB_CONFIG"));
        SidebarTabButton(PanelId::Hotkeys, Localization::T("TAB_HOTKEYS"));
        SidebarTabButton(PanelId::About, Localization::T("TAB_ABOUT"));
        ImGui::SetWindowFontScale(1.0f);

        EndSurface();
    }

    void RenderQuickActionsPane()
    {
        if (!BeginSurface("##quick_actions", Localization::T("QUICK_ACTIONS")))
        {
            EndSurface();
            return;
        }

        using namespace ConfigManager;
        switch (g_CurrentPanel)
        {
        case PanelId::Player:
            AnimatedToggle(Localization::T("GODMODE"), &B("Player.GodMode"));
            AnimatedToggle(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
            AnimatedToggle(Localization::T("INF_GRENADES"), &B("Player.InfGrenades"));
            AnimatedToggle(Localization::T("NO_TARGET"), &B("Player.NoTarget"));
            AnimatedToggle(Localization::T("DEMIGOD"), &B("Player.Demigod"));
            AnimatedToggle(Localization::T("SPEED_HACK"), &B("Player.SpeedEnabled"));
            AnimatedToggle(Localization::T("FLIGHT"), &B("Player.Flight"));
            AnimatedToggle(Localization::T("VEHICLE_SPEED_HACK"), &B("Player.VehicleSpeedEnabled"));
            AnimatedToggle(Localization::T("INF_GLIDE_STAMINA"), &B("Player.InfGlideStamina"));
            break;
        case PanelId::Camera:
            {
                bool thirdPerson = B("Player.ThirdPerson");
                if (AnimatedToggle(Localization::T("THIRD_PERSON"), &thirdPerson))
                    Cheats::ToggleThirdPerson();
            }
            {
                bool freeCam = B("Player.Freecam");
                if (AnimatedToggle(Localization::T("FREE_CAM"), &freeCam))
                    Cheats::ToggleFreecam();
            }
            AnimatedToggle(Localization::T("ENABLE_FOV_CHANGER"), &B("Misc.EnableFOV"));
            AnimatedToggle(Localization::T("ENABLE_VIEWMODEL_FOV"), &B("Misc.EnableViewModelFOV"));
            AnimatedToggle(Localization::T("ENABLE_RETICLE"), &B("Misc.Reticle"));
            break;
        case PanelId::World:
            if (AnimatedButton(Localization::T("KILL_ENEMIES"))) Cheats::KillEnemies();
            if (AnimatedButton(Localization::T("TELEPORT_LOOT"))) Cheats::TeleportLoot();
            if (AnimatedButton(Localization::T("CLEAR_GROUND_ITEMS"))) Cheats::ClearGroundItems();
            if (AnimatedToggle(Localization::T("PLAYERS_ONLY"), &B("Player.PlayersOnly")))
                Cheats::TogglePlayersOnly();
            AnimatedToggle(Localization::T("MAP_TELEPORT"), &B("Misc.MapTeleport"));
            break;
        case PanelId::Aimbot:
            AnimatedToggle(Localization::T("AIMBOT"), &B("Aimbot.Enabled"));
            AnimatedToggle(Localization::T("SILENT_AIM"), &B("Aimbot.Silent"));
            AnimatedToggle(Localization::T("SMOOTH_AIM"), &B("Aimbot.Smooth"));
            AnimatedToggle(Localization::T("DRAW_FOV"), &B("Aimbot.DrawFOV"));
            AnimatedToggle(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));
            break;
        case PanelId::Weapon:
            AnimatedToggle(Localization::T("INSTANT_HIT"), &B("Weapon.InstantHit"));
            AnimatedToggle(Localization::T("RAPID_FIRE"), &B("Weapon.RapidFire"));
            AnimatedToggle(Localization::T("NO_RECOIL"), &B("Weapon.NoRecoil"));
            AnimatedToggle(Localization::T("NO_SPREAD"), &B("Weapon.NoSpread"));
            AnimatedToggle(Localization::T("NO_AMMO_CONSUME"), &B("Weapon.NoAmmoConsume"));
            break;
        case PanelId::Esp:
            AnimatedToggle(Localization::T("SHOW_BOX"), &B("ESP.ShowBox"));
            AnimatedToggle(Localization::T("SHOW_DISTANCE"), &B("ESP.ShowEnemyDistance"));
            AnimatedToggle(Localization::T("SHOW_BONES"), &B("ESP.Bones"));
            AnimatedToggle(Localization::T("SHOW_LOOT_NAME"), &B("ESP.ShowLootName"));
            AnimatedToggle(Localization::T("SHOW_INTERACTIVES"), &B("ESP.ShowInteractives"));
            break;
        case PanelId::ThemeStudio:
            LocalizedColorEditor(Localization::T("THEME_ACCENT"), (float*)&ConfigManager::Color("Misc.ThemeAccent"), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            LocalizedColorEditor(Localization::T("THEME_GLOW"), (float*)&ConfigManager::Color("Misc.ThemeGlow"), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            LocalizedColorEditor(Localization::T("THEME_TINT"), (float*)&ConfigManager::Color("Misc.ThemeTint"), ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);
            break;
        case PanelId::Misc:
            AnimatedToggle(Localization::T("SHOW_ACTIVE_FEATURES"), &B("Misc.RenderOptions"));
            AnimatedToggle(Localization::T("DISABLE_VOLUMETRIC_CLOUDS"), &B("Misc.DisableVolumetricClouds"));
            break;
        case PanelId::Hotkeys:
            ImGui::TextWrapped("%s", Localization::T("HOTKEY_TAB_HELP"));
            break;
        case PanelId::About:
        default:
            AnimatedToggle(Localization::T("ESP"), &B("Player.ESP"));
            AnimatedToggle(Localization::T("AIMBOT"), &B("Aimbot.Enabled"));
            AnimatedToggle(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));
            AnimatedToggle(Localization::T("GODMODE"), &B("Player.GodMode"));
            AnimatedToggle(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
            break;
        }

        ImGui::Spacing();
        if (AnimatedButton(Localization::T("SAVE_SETTINGS"), ImVec2(0.0f, 36.0f * GetMenuUiScale()), true))
            ConfigManager::SaveSettings();
        if (AnimatedButton(Localization::T("LOAD_SETTINGS"), ImVec2(0.0f, 36.0f * GetMenuUiScale())))
        {
            ConfigManager::LoadSettings();
            GUI::ThemeManager::ApplyByIndex(ConfigManager::I("Misc.Theme"));
        }
        EndSurface();
    }

    void RenderThemePreviewPane()
    {
        if (!BeginSurface("##theme_preview", Localization::T("VISUAL_TUNING")))
        {
            EndSurface();
            return;
        }

        ImGui::TextDisabled("%s", Localization::T("VISUAL_TUNING_HINT"));
        const float scale = GetMenuUiScale();
        const ImVec2 cardPos = ImGui::GetCursorScreenPos();
        const float cardWidth = ImGui::GetContentRegionAvail().x;
        const float cardHeight = 132.0f * scale;
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        const ImVec4 tint = ConfigManager::Color("Misc.ThemeTint");
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight), ColorToU32(ImVec4(tint.x, tint.y, tint.z, 0.22f + tint.w * 0.28f)), 20.0f * scale);
        drawList->AddRect(cardPos, ImVec2(cardPos.x + cardWidth, cardPos.y + cardHeight), ColorToU32(glow, 0.30f), 20.0f * scale, 0, 1.5f);
        drawList->AddRect(ImVec2(cardPos.x - 2.0f, cardPos.y - 2.0f), ImVec2(cardPos.x + cardWidth + 2.0f, cardPos.y + cardHeight + 2.0f), ColorToU32(glow, 0.14f), 22.0f * scale, 0, 5.0f);
        drawList->AddRectFilled(ImVec2(cardPos.x + 18.0f * scale, cardPos.y + 18.0f * scale), ImVec2(cardPos.x + cardWidth - 18.0f * scale, cardPos.y + 22.0f * scale), ColorToU32(accent, 0.72f), 3.0f * scale);
        drawList->AddText(ImVec2(cardPos.x + 18.0f * scale, cardPos.y + 36.0f * scale), ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.96f)), Localization::T("MENU_TITLE"));
        drawList->AddText(ImVec2(cardPos.x + 18.0f * scale, cardPos.y + 66.0f * scale), ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.72f)), Localization::T("THEME_STUDIO"));
        drawList->AddRectFilled(ImVec2(cardPos.x + 18.0f * scale, cardPos.y + 94.0f * scale), ImVec2(cardPos.x + 138.0f * scale, cardPos.y + 116.0f * scale), ColorToU32(accent, 0.55f), 10.0f * scale);
        drawList->AddRect(ImVec2(cardPos.x + 18.0f * scale, cardPos.y + 94.0f * scale), ImVec2(cardPos.x + 138.0f * scale, cardPos.y + 116.0f * scale), ColorToU32(glow, 0.36f), 10.0f * scale, 0, 1.0f);
        drawList->AddText(ImVec2(cardPos.x + 160.0f * scale, cardPos.y + 95.0f * scale), ColorToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.84f)), Localization::T("VISUAL_TUNING"));
        ImGui::Dummy(ImVec2(cardWidth, cardHeight + 10.0f * scale));

        LocalizedColorEditor(Localization::T("RETICLE_COLOR"), (float*)&ConfigManager::Color("Misc.ReticleColor"), ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("ENEMY_COLOR"), (float*)&ConfigManager::Color("ESP.EnemyColor"), ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("TEAM_COLOR"), (float*)&ConfigManager::Color("ESP.TeamColor"), ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("TRACER_COLOR"), (float*)&ConfigManager::Color("ESP.TracerColor"), ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("LOOT_COLOR"), (float*)&ConfigManager::Color("ESP.LootColor"), ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("INTERACTIVE_COLOR"), (float*)&ConfigManager::Color("ESP.InteractiveColor"), ImGuiColorEditFlags_AlphaBar);
        EndSurface();
    }

    void RenderThemeStudioSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("COLOR_SETTINGS"));
        LocalizedColorEditor(Localization::T("THEME_ACCENT"), (float*)&Color("Misc.ThemeAccent"), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("THEME_GLOW"), (float*)&Color("Misc.ThemeGlow"), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        LocalizedColorEditor(Localization::T("THEME_TINT"), (float*)&Color("Misc.ThemeTint"), ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar);

        SectionHeader(Localization::T("VISUAL_TUNING"));
        ImGui::SliderFloat(Localization::T("THEME_GLOW_STRENGTH"), &F("Misc.ThemeGlowStrength"), 0.0f, 1.6f, "%.2f");
        ImGui::SliderFloat(Localization::T("THEME_GLOW_SPREAD"), &F("Misc.ThemeGlowSpread"), 4.0f, 56.0f, "%.0f");
        ImGui::SliderFloat(Localization::T("THEME_BACKDROP"), &F("Misc.ThemeBackdropOpacity"), 0.25f, 1.0f, "%.2f");

        SectionHeader(Localization::T("CONFIG_ACTIONS"));
        if (AnimatedButton(Localization::T("SAVE_SETTINGS")))
            SaveSettings();
        ImGui::SameLine();
        if (AnimatedButton(Localization::T("LOAD_SETTINGS")))
        {
            LoadSettings();
            GUI::ThemeManager::ApplyByIndex(I("Misc.Theme"));
        }
    }

    void RenderContextPane()
    {
        switch (g_CurrentPanel)
        {
        case PanelId::ThemeStudio:
            RenderThemePreviewPane();
            break;
        case PanelId::Misc:
        case PanelId::Hotkeys:
        case PanelId::About:
            RenderQuickActionsPane();
            break;
        default:
            RenderQuickActionsPane();
            break;
        }
    }

    void SectionHeader(const char* label)
    {
        ImGui::Spacing();
        ImGui::SeparatorText(label);
        const float scale = GetMenuUiScale();
        const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec4 accent = ConfigManager::Color("Misc.ThemeAccent");
        const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
        const float lineY = rect.Min.y + rect.GetHeight() * 0.5f;
        const float rightLimit = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 24.0f * scale;
        SubmitTextGlow(rect.Min, ImVec2(rect.GetWidth(), rect.GetHeight()), glow, 0.18f);
        DrawGlowRect(
            drawList,
            ImVec2(rect.Min.x - 2.0f * scale, rect.Min.y + 2.0f * scale),
            ImVec2(rect.Max.x + 8.0f * scale, rect.Max.y - 2.0f * scale),
            glow,
            8.0f * scale,
            0.16f,
            0.9f);
        drawList->AddLine(
            ImVec2(rect.Max.x + 8.0f * scale, lineY),
            ImVec2(rightLimit, lineY),
            ColorToU32(glow, 0.20f),
            2.0f);
        drawList->AddRectFilled(
            ImVec2(rect.Min.x, rect.Max.y - 3.0f),
            ImVec2((std::min)(rect.Min.x + 84.0f * scale, rightLimit), rect.Max.y - 1.0f),
            ColorToU32(accent, 0.46f),
            2.0f);
    }

    ImVec4 GetHookStatusColor(const char* statusKey)
    {
        if (std::strcmp(statusKey, "HOOK_STATUS_HOOKED") == 0)
            return ImVec4(0.48f, 0.92f, 0.75f, 0.96f);
        if (std::strcmp(statusKey, "HOOK_STATUS_FAILED") == 0)
            return ImVec4(1.00f, 0.46f, 0.46f, 0.96f);
        if (std::strcmp(statusKey, "HOOK_STATUS_WAITING") == 0)
            return ImVec4(0.64f, 0.76f, 0.98f, 0.92f);
        if (std::strcmp(statusKey, "HOOK_STATUS_RESOLVED") == 0)
            return ImVec4(0.82f, 0.90f, 0.98f, 0.92f);
        return ImVec4(0.88f, 0.74f, 0.36f, 0.92f);
    }

    void RenderHookStatusRow(const char* label, const char* statusKey, const char* detail = nullptr)
    {
        ImGui::Text("%s", label);
        ImGui::SameLine(220.0f * GetMenuUiScale());
        ImGui::TextColored(GetHookStatusColor(statusKey), "%s", Localization::T(statusKey));
        if (detail && detail[0] != '\0')
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", detail);
        }
        ImGui::Separator();
    }

    void RenderAboutSection()
    {
        ImGui::Text(Localization::T("MENU_TITLE"));
        ImGui::Text(Localization::T("VERSION_D_D_D"), MAJORVERSION, MINORVERSION, PATCHVERSION);

        ImGui::Spacing();
        if (GVars.PlayerController && GVars.PlayerController->PlayerState)
        {
            APlayerState* PlayerState = GVars.PlayerController->PlayerState;
            std::string PlayerName = PlayerState->GetPlayerName().ToString();
            ImGui::Text(Localization::T("THANKS_FOR_USING_THIS_CHEAT_S"), PlayerName.c_str());
        }
        else
        {
            ImGui::Text(Localization::T("USERNAME_NOT_FOUND_BUT_THANKS_FOR_USING_ANYWAY"));
        }

        ImGui::Spacing();
        ImGui::SeparatorText(Localization::T("SIGNATURE_HOOK_STATUS"));

        const std::vector<SignatureRegistry::SignatureSnapshot> hookSnapshots = SignatureRegistry::GetSnapshots();
        if (hookSnapshots.empty())
        {
            ImGui::TextDisabled("%s", Localization::T("NO_SIGNATURE_HOOKS_REGISTERED"));
        }
        else
        {
            for (const auto& hook : hookSnapshots)
            {
                const char* statusKey = "HOOK_STATUS_PENDING";

                if (hook.bHookInstalled)
                    statusKey = "HOOK_STATUS_HOOKED";
                else if (hook.bResolveFailed)
                    statusKey = "HOOK_STATUS_FAILED";
                else if (!hook.bTimingReady)
                    statusKey = "HOOK_STATUS_WAITING";
                else if (hook.CachedAddress)
                    statusKey = "HOOK_STATUS_RESOLVED";

                ImGui::PushID(hook.Name.c_str());

                char detail[96]{};
                const char* timingKey = hook.Timing == SignatureRegistry::HookTiming::InGameReady
                    ? "HOOK_TIMING_INGAME_READY"
                    : "HOOK_TIMING_IMMEDIATE";
                const char* readyKey = hook.bTimingReady ? "HOOK_READY" : "HOOK_NOT_READY";

                if (hook.CachedAddress)
                {
                    _snprintf_s(
                        detail,
                        sizeof(detail),
                        _TRUNCATE,
                        "%s | %s | 0x%llX",
                        Localization::T(timingKey),
                        Localization::T(readyKey),
                        static_cast<unsigned long long>(hook.CachedAddress));
                }
                else
                {
                    _snprintf_s(
                        detail,
                        sizeof(detail),
                        _TRUNCATE,
                        "%s | %s",
                        Localization::T(timingKey),
                        Localization::T(readyKey));
                }

                RenderHookStatusRow(hook.Name.c_str(), statusKey, detail);
                ImGui::PopID();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText(Localization::T("ENGINE_HOOK_STATUS"));

        const Hooks::State& hookState = Hooks::GetState();
        const bool bProcessEventHooked = hookState.pcVTable != nullptr && oProcessEvent != nullptr;
        const bool bPlayerStateHooked = hookState.psVTable != nullptr;
        const bool bPresentHooked = d3d12hook::oPresentD3D12 != nullptr || d3d12hook::oPresent1D3D12 != nullptr;
        const bool bCommandQueueHooked = d3d12hook::oExecuteCommandListsD3D12 != nullptr;
        const bool bPresentActive = g_PresentCount.load() > 0;

        RenderHookStatusRow(
            Localization::T("ENGINE_HOOK_PROCESS_EVENT"),
            bProcessEventHooked ? "HOOK_STATUS_HOOKED" : "HOOK_STATUS_PENDING");
        RenderHookStatusRow(
            Localization::T("ENGINE_HOOK_PLAYERSTATE"),
            bPlayerStateHooked ? "HOOK_STATUS_HOOKED" : "HOOK_STATUS_PENDING");
        RenderHookStatusRow(
            Localization::T("ENGINE_HOOK_PRESENT"),
            bPresentHooked ? (bPresentActive ? "HOOK_STATUS_HOOKED" : "HOOK_STATUS_RESOLVED") : "HOOK_STATUS_PENDING",
            bPresentActive ? Localization::T("ENGINE_HOOK_PRESENT_ACTIVE") : nullptr);
        RenderHookStatusRow(
            Localization::T("ENGINE_HOOK_COMMAND_QUEUE"),
            bCommandQueueHooked ? "HOOK_STATUS_HOOKED" : "HOOK_STATUS_PENDING");
    }

    void RenderPlayerSection()
    {
        using namespace ConfigManager;
        static int xpAmount = 1;
        static int currencyAmount = 1000;

        SectionHeader(Localization::T("CORE_FEATURES"));
        AnimatedToggle(Localization::T("GODMODE"), &B("Player.GodMode"));
        AnimatedToggle(Localization::T("DEMIGOD"), &B("Player.Demigod"));
        AnimatedToggle(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
        AnimatedToggle(Localization::T("INF_GRENADES"), &B("Player.InfGrenades"));
        AnimatedToggle(Localization::T("NO_TARGET"), &B("Player.NoTarget"));

        SectionHeader(Localization::T("MOVEMENT"));
        AnimatedToggle(Localization::T("SPEED_HACK"), &B("Player.SpeedEnabled"));
        if (B("Player.SpeedEnabled"))
        {
            ImGui::SliderFloat(Localization::T("SPEED_VALUE"), &F("Player.Speed"), 1.0f, 10.0f, "%.1f");
        }
        AnimatedToggle(Localization::T("FLIGHT"), &B("Player.Flight"));
        if (B("Player.Flight"))
        {
            ImGui::SliderFloat(Localization::T("FLIGHT_SPEED"), &F("Player.FlightSpeed"), 1.0f, 20.0f, "%.1f");
        }
        AnimatedToggle(Localization::T("VEHICLE_SPEED_HACK"), &B("Player.VehicleSpeedEnabled"));
        if (B("Player.VehicleSpeedEnabled"))
        {
            ImGui::SliderFloat(Localization::T("VEHICLE_SPEED_VALUE"), &F("Player.VehicleSpeed"), 1.0f, 20.0f, "%.1f");
        }
        AnimatedToggle(Localization::T("INF_GLIDE_STAMINA"), &B("Player.InfGlideStamina"));

        SectionHeader(Localization::T("PLAYER_PROGRESSION"));
        ImGui::InputInt(Localization::T("EXPERIENCE_LEVEL"), &xpAmount);
        if (AnimatedButton(Localization::T("SET_EXPERIENCE_LEVEL")))
        {
            Cheats::SetExperienceLevel(xpAmount);
        }
        if (AnimatedButton(Localization::T("GIVE_5_LEVELS")))
        {
            Cheats::GiveLevels();
        }

        SectionHeader(Localization::T("CURRENCY_SETTINGS"));
        ImGui::InputInt("##player_currency_amount", &currencyAmount);
        if (AnimatedButton(Localization::T("CASH")))
            Cheats::AddCurrency("Cash", currencyAmount);
        ImGui::SameLine();
        if (AnimatedButton(Localization::T("ERIDIUM")))
            Cheats::AddCurrency("eridium", currencyAmount);
        ImGui::SameLine();
        if (AnimatedButton(Localization::T("VC_TICKETS")))
            Cheats::AddCurrency("VaultCard01_Tokens", currencyAmount);
    }

    void RenderCameraSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("CORE_FEATURES"));
        const bool bTPEnabled = B("Player.ThirdPerson");
        const bool bFreecamEnabled = B("Player.Freecam");

        if (bFreecamEnabled)
        {
            bool bFreecam = B("Player.Freecam");
            if (AnimatedToggle(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
            AnimatedToggle(Localization::T("FREECAM_BLOCK_INPUT"), &B("Misc.FreecamBlockInput"));
        }
        else if (bTPEnabled)
        {
            bool bTP = B("Player.ThirdPerson");
            if (AnimatedToggle(Localization::T("THIRD_PERSON"), &bTP)) Cheats::ToggleThirdPerson();
            AnimatedToggle(Localization::T("THIRD_PERSON_CENTERED"), &B("Misc.ThirdPersonCentered"));

            ImGui::Indent();
            bool bOTS = B("Player.OverShoulder");
            if (AnimatedToggle(Localization::T("OVER_SHOULDER"), &bOTS))
            {
                B("Player.OverShoulder") = bOTS;
            }
            if (B("Player.OverShoulder"))
            {
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_X"), &F("Misc.OTS_X"), -500.0f, 500.0f);
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_Y"), &F("Misc.OTS_Y"), -200.0f, 200.0f);
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_Z"), &F("Misc.OTS_Z"), -200.0f, 200.0f);
                AnimatedToggle(Localization::T("OTS_ADS_CAMERA_OVERRIDE"), &B("Misc.OTSADSOverride"));
                if (B("Misc.OTSADSOverride"))
                {
                    AnimatedToggle(Localization::T("ADS_FIRST_PERSON"), &B("Misc.OTSADSFirstPerson"));
                    if (!B("Misc.OTSADSFirstPerson"))
                    {
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_X"), &F("Misc.OTSADS_X"), -500.0f, 500.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_Y"), &F("Misc.OTSADS_Y"), -200.0f, 200.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_Z"), &F("Misc.OTSADS_Z"), -200.0f, 200.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_FOV"), &F("Misc.OTSADSFOV"), 20.0f, 180.0f, "%.1f");
                        ImGui::SliderFloat(Localization::T("OTS_ADS_BLEND_TIME"), &F("Misc.OTSADSBlendTime"), 0.01f, 1.00f, "%.2fs");
                    }
                }
            }
            else
            {
                AnimatedToggle(Localization::T("ADS_FIRST_PERSON"), &B("Misc.ThirdPersonADSFirstPerson"));
            }
            ImGui::Unindent();
        }
        else
        {
            bool bTP = B("Player.ThirdPerson");
            if (AnimatedToggle(Localization::T("THIRD_PERSON"), &bTP))
            {
                Cheats::ToggleThirdPerson();
            }

            bool bFreecam = B("Player.Freecam");
            if (AnimatedToggle(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
        }

        SectionHeader(Localization::T("VIEW_SETTINGS"));
        if (AnimatedToggle(Localization::T("ENABLE_FOV_CHANGER"), &B("Misc.EnableFOV")))
        {
            if (B("Misc.EnableFOV") && GVars.POV)
            {
                F("Misc.FOV") = GVars.POV->fov;
            }
        }
        if (B("Misc.EnableFOV"))
        {
            ImGui::SliderFloat(Localization::T("FOV_VALUE"), &F("Misc.FOV"), 60.0f, 180.0f);
            ImGui::SliderFloat(Localization::T("ADS_ZOOM_SCALE"), &F("Misc.ADSFOVScale"), 0.2f, 1.0f, "%.2f");
        }
        AnimatedToggle(Localization::T("ENABLE_VIEWMODEL_FOV"), &B("Misc.EnableViewModelFOV"));
        if (B("Misc.EnableViewModelFOV"))
        {
            ImGui::SliderFloat(Localization::T("VIEWMODEL_FOV_VALUE"), &F("Misc.ViewModelFOV"), 60.0f, 150.0f);
        }

        SectionHeader(Localization::T("RETICLE_SETTINGS"));
        AnimatedToggle(Localization::T("ENABLE_RETICLE"), &B("Misc.Reticle"));
        if (B("Misc.Reticle"))
        {
            AnimatedToggle(Localization::T("RETICLE_CROSSHAIR"), &B("Misc.CrossReticle"));
            ImGui::SliderFloat(Localization::T("RETICLE_SIZE"), &F("Misc.ReticleSize"), 2.0f, 30.0f, "%.1f");
            ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_X"), &Vec2("Misc.ReticlePosition").x, -200.0f, 200.0f, "%.0f");
            ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_Y"), &Vec2("Misc.ReticlePosition").y, -200.0f, 200.0f, "%.0f");
            LocalizedColorEditor(Localization::T("RETICLE_COLOR"), (float*)&Color("Misc.ReticleColor"), ImGuiColorEditFlags_NoInputs);
        }
    }

    void RenderWorldSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("WORLD_ACTIONS"));
        if (AnimatedButton(Localization::T("TELEPORT_LOOT"))) Cheats::TeleportLoot();
        if (AnimatedButton(Localization::T("SPAWN_ITEMS"))) Cheats::SpawnItems();
        if (AnimatedButton(Localization::T("KILL_ENEMIES"))) Cheats::KillEnemies();
        if (AnimatedButton(Localization::T("CLEAR_GROUND_ITEMS"))) Cheats::ClearGroundItems();

        SectionHeader(Localization::T("WORLD_SIMULATION"));
        if (AnimatedToggle(Localization::T("PLAYERS_ONLY"), &B("Player.PlayersOnly"))) Cheats::TogglePlayersOnly();

        if (ImGui::SliderFloat(Localization::T("GAME_SPEED"), &F("Player.GameSpeed"), 0.1f, 10.0f))
            Cheats::SetGameSpeed(F("Player.GameSpeed"));

        SectionHeader(Localization::T("TELEPORT_SETTINGS"));
        AnimatedToggle(Localization::T("MAP_TELEPORT"), &B("Misc.MapTeleport"));
        GUI::AddDefaultTooltip("Quickly make and remove a pin on the map to teleport to that location.");
        ImGui::SliderFloat(Localization::T("MAP_TELEPORT_WINDOW"), &F("Misc.MapTPWindow"), 0.5f, 5.0f);
    }

    void RenderAimbotSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("CORE_FEATURES"));
        AnimatedToggle(Localization::T("AIMBOT"), &B("Aimbot.Enabled"));
        AnimatedToggle(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));

        SectionHeader(Localization::T("STANDARD_AIMBOT_SETTINGS"));
        AnimatedToggle(Localization::T("REQUIRE_LOS"), &B("Aimbot.LOS"));
        AnimatedToggle(Localization::T("REQUIRE_KEY_HELD"), &B("Aimbot.RequireKeyHeld"));
        AnimatedToggle(Localization::T("TARGET_ALL"), &B("Aimbot.TargetAll"));
        AnimatedToggle(Localization::T("SILENT_AIM"), &B("Aimbot.Silent"));
        if (B("Aimbot.Silent"))
        {
            ImGui::Indent();
            AnimatedToggle(Localization::T("USE_NATIVE_PROJECTILE_HOOK"), &B("Aimbot.NativeProjectileHook"));
            AnimatedToggle(Localization::T("MAGIC_BULLETS"), &B("Aimbot.Magic"));
            ImGui::Unindent();
        }
        AnimatedToggle(Localization::T("SMOOTH_AIM"), &B("Aimbot.Smooth"));
        if (B("Aimbot.Smooth"))
            ImGui::SliderFloat(Localization::T("SMOOTHING"), &F("Aimbot.SmoothingVector"), 1.0f, 20.0f);

        SectionHeader(Localization::T("TARGET_MODE"));
        ImGui::SliderFloat(Localization::T("AIMBOT_FOV"), &F("Aimbot.MaxFOV"), 1.0f, 180.0f);
        ImGui::SliderFloat(Localization::T("MIN_DISTANCE"), &F("Aimbot.MinDistance"), 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat(Localization::T("MAX_DISTANCE"), &F("Aimbot.MaxDistance"), 1.0f, 500.0f);
        int& targetMode = I("Aimbot.TargetMode");
        targetMode = (std::clamp)(targetMode, 0, 1);
        if (ImGui::BeginCombo(Localization::T("TARGET_MODE"), GetTargetModeLabel(targetMode)))
        {
            const bool isScreenSelected = (targetMode == 0);
            if (ImGui::Selectable(Localization::T("TARGET_MODE_SCREEN"), isScreenSelected))
                targetMode = 0;
            if (isScreenSelected) ImGui::SetItemDefaultFocus();

            const bool isDistanceSelected = (targetMode == 1);
            if (ImGui::Selectable(Localization::T("TARGET_MODE_DISTANCE"), isDistanceSelected))
                targetMode = 1;
            if (isDistanceSelected) ImGui::SetItemDefaultFocus();
            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo(Localization::T("TARGET_BONE"), S("Aimbot.Bone").c_str()))
        {
            for (auto& pair : BoneOptions)
            {
                bool is_selected = (S("Aimbot.Bone") == pair.second);
                if (ImGui::Selectable(pair.first, is_selected))
                    S("Aimbot.Bone") = pair.second;
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        SectionHeader(Localization::T("VISUAL_ASSIST"));
        AnimatedToggle(Localization::T("DRAW_FOV"), &B("Aimbot.DrawFOV"));
        if (B("Aimbot.DrawFOV"))
            ImGui::SliderFloat(Localization::T("FOV_LINE_THICKNESS"), &F("Aimbot.FOVThickness"), 1.0f, 6.0f, "%.1f");
        AnimatedToggle(Localization::T("DRAW_ARROW"), &B("Aimbot.DrawArrow"));
        if (B("Aimbot.DrawArrow"))
            ImGui::SliderFloat(Localization::T("ARROW_LINE_THICKNESS"), &F("Aimbot.ArrowThickness"), 1.0f, 6.0f, "%.1f");

        SectionHeader(Localization::T("TRIGGERBOT"));
        if (B("Trigger.Enabled"))
        {
            ImGui::Indent();
            AnimatedToggle(Localization::T("REQUIRE_KEY_HELD"), &B("Trigger.RequireKeyHeld"));
            AnimatedToggle(Localization::T("TARGET_ALL"), &B("Trigger.TargetAll"));
            ImGui::Unindent();
        }
    }

    void RenderWeaponSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("BALLISTICS"));
        AnimatedToggle(Localization::T("INSTANT_HIT"), &B("Weapon.InstantHit"));
        GUI::AddDefaultTooltip("Increases bullet speed to effectively hit targets instantly.");
        if (B("Weapon.InstantHit"))
        {
            ImGui::SliderFloat(Localization::T("PROJECTILE_SPEED"), &F("Weapon.ProjectileSpeedMultiplier"), 1.0f, 9999.0f);
        }

        SectionHeader(Localization::T("FIRING"));
        AnimatedToggle(Localization::T("RAPID_FIRE"), &B("Weapon.RapidFire"));
        if (B("Weapon.RapidFire"))
        {
            ImGui::SliderFloat(Localization::T("FIRE_RATE_MODIFIER"), &F("Weapon.FireRate"), 0.1f, 10.0f, "%.1f");
        }

        SectionHeader(Localization::T("STABILITY"));
        AnimatedToggle(Localization::T("NO_RECOIL"), &B("Weapon.NoRecoil"));
        if (B("Weapon.NoRecoil"))
        {
            ImGui::SliderFloat(Localization::T("RECOIL_REDUCTION"), &F("Weapon.RecoilReduction"), 0.0f, 1.0f);
        }
        AnimatedToggle(Localization::T("NO_SPREAD"), &B("Weapon.NoSpread"));
        AnimatedToggle(Localization::T("NO_SWAY"), &B("Weapon.NoSway"));

        SectionHeader(Localization::T("AMMO_HANDLING"));
        AnimatedToggle(Localization::T("INSTANT_RELOAD"), &B("Weapon.InstantReload"));
        GUI::HostOnlyTooltip();
        AnimatedToggle(Localization::T("INSTANT_SWAP"), &B("Weapon.InstantSwap"));
        GUI::HostOnlyTooltip();
        AnimatedToggle(Localization::T("NO_AMMO_CONSUME"), &B("Weapon.NoAmmoConsume"));
        GUI::HostOnlyTooltip();
    }

    void RenderMiscSection()
    {
        using namespace ConfigManager;

        SectionHeader(Localization::T("GENERAL_SETTINGS"));
        static const char* HostLangs[] = { "English", "简体中文" };
        int& CurrentLangIdx = ConfigManager::I("Misc.Language");
        CurrentLangIdx = (std::clamp)(CurrentLangIdx, 0, IM_ARRAYSIZE(HostLangs) - 1);
        if (ImGui::Combo(Localization::T("LANGUAGE"), &CurrentLangIdx, HostLangs, IM_ARRAYSIZE(HostLangs)))
        {
            Localization::CurrentLanguage = (Language)CurrentLangIdx;
        }

        SectionHeader(Localization::T("VIEW_SETTINGS"));
        AnimatedToggle(Localization::T("SHOW_ACTIVE_FEATURES"), &B("Misc.RenderOptions"));
        AnimatedToggle(Localization::T("DISABLE_VOLUMETRIC_CLOUDS"), &B("Misc.DisableVolumetricClouds"));

#if BL4_DEBUG_BUILD
        SectionHeader(Localization::T("DEBUG"));
        if (ImGui::TreeNode(Localization::T("DEBUG")))
        {
            AnimatedToggle(Localization::T("ENABLE_EVENT_DEBUG_LOGS"), &B("Misc.Debug"));
            AnimatedToggle(Localization::T("ENABLE_PING_DUMP"), &B("Misc.PingDump"));

            bool bRecording = Logger::IsRecording();
            if (AnimatedToggle(Localization::T("ENABLE_EVENT_RECORDING"), &bRecording))
            {
                if (bRecording) Logger::StartRecording();
                else Logger::StopRecording();
            }

            if (AnimatedButton(Localization::T("DUMP_GOBJECTS"))) Cheats::DumpObjects();
            ImGui::TreePop();
        }
#endif

        SectionHeader(Localization::T("CONFIG_ACTIONS"));
        if (AnimatedButton(Localization::T("SAVE_SETTINGS")))
            ConfigManager::SaveSettings();
        ImGui::SameLine();
        if (AnimatedButton(Localization::T("LOAD_SETTINGS")))
        {
            ConfigManager::LoadSettings();
            GUI::ThemeManager::ApplyByIndex(ConfigManager::I("Misc.Theme"));
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        ImGui::TextWrapped(Localization::T("VOLATILE_HINT"));
        ImGui::PopStyleColor();
    }

    void RenderEspPanel()
    {
        using namespace ConfigManager;
        SectionHeader(Localization::T("CORE_FEATURES"));
        AnimatedToggle(Localization::T("ESP"), &B("Player.ESP"));
        SectionHeader(Localization::T("ENEMY_ESP"));
        AnimatedToggle(Localization::T("SHOW_TEAM"), &B("ESP.ShowTeam"));
        AnimatedToggle(Localization::T("SHOW_BOX"), &B("ESP.ShowBox"));
        AnimatedToggle(Localization::T("SHOW_DISTANCE"), &B("ESP.ShowEnemyDistance"));
        AnimatedToggle(Localization::T("SHOW_BONES"), &B("ESP.Bones"));
        AnimatedToggle(Localization::T("SHOW_NAME"), &B("ESP.ShowEnemyName"));
        AnimatedToggle(Localization::T("SHOW_ENEMY_INDICATOR"), &B("ESP.ShowEnemyIndicator"));

        SectionHeader(Localization::T("TRACER_SETTINGS"));
        AnimatedToggle(Localization::T("SHOW_BULLET_TRACERS"), &B("ESP.BulletTracers"));
        if (B("ESP.BulletTracers"))
        {
            AnimatedToggle(Localization::T("TRACER_RAINBOW"), &B("ESP.TracerRainbow"));
            ImGui::SliderFloat(Localization::T("TRACER_DURATION"), &F("ESP.TracerDuration"), 0.1f, 8.0f, "%.1f s");
            if (!B("ESP.TracerRainbow"))
            {
                LocalizedColorEditor(Localization::T("TRACER_COLOR"), (float*)&Color("ESP.TracerColor"), ImGuiColorEditFlags_NoInputs);
            }
        }

        SectionHeader(Localization::T("LOOT_INTERACTIVES"));
        AnimatedToggle(Localization::T("SHOW_LOOT_NAME"), &B("ESP.ShowLootName"));
        if (B("ESP.ShowLootName"))
        {
            ImGui::SliderFloat(Localization::T("LOOT_MAX_DISTANCE"), &F("ESP.LootMaxDistance"), 10.0f, 1000.0f, "%.0f");
            LocalizedColorEditor(Localization::T("LOOT_COLOR"), (float*)&Color("ESP.LootColor"), ImGuiColorEditFlags_NoInputs);
        }

        AnimatedToggle(Localization::T("SHOW_INTERACTIVES"), &B("ESP.ShowInteractives"));
        if (B("ESP.ShowInteractives"))
        {
            ImGui::SliderFloat(Localization::T("INTERACTIVE_MAX_DISTANCE"), &F("ESP.InteractiveMaxDistance"), 10.0f, 1000.0f, "%.0f");
            LocalizedColorEditor(Localization::T("INTERACTIVE_COLOR"), (float*)&Color("ESP.InteractiveColor"), ImGuiColorEditFlags_NoInputs);
        }

        SectionHeader(Localization::T("COLOR_SETTINGS"));
        LocalizedColorEditor(Localization::T("ENEMY_COLOR"), (float*)&Color("ESP.EnemyColor"), ImGuiColorEditFlags_NoInputs);
        if (B("ESP.ShowTeam"))
        {
            LocalizedColorEditor(Localization::T("TEAM_COLOR"), (float*)&Color("ESP.TeamColor"), ImGuiColorEditFlags_NoInputs);
        }
    }
}

void GUI::AddDefaultTooltip(const char* desc)
{
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void GUI::HostOnlyTooltip()
{
	AddDefaultTooltip(Localization::T("HOST_ONLY_IF_YOU_ARE_NOT_THE_HOST_THIS_FEATURE_WILL_NOT_FUNCTION"));
}

void GUI::RenderMenu()
{
    GUI::BackdropBlur::BeginGlowFrame();
    if (!ShowMenu)
    {
        g_MenuBackdropState.Visible = false;
        return;
    }

    GUI::ThemeManager::ApplyRuntimeAccent();

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float scale = GetMenuUiScale();
    const ImVec2 windowSize(
        (std::min)(displaySize.x - 80.0f * scale, 1420.0f * scale),
        (std::min)(displaySize.y - 70.0f * scale, 920.0f * scale));
    const ImVec2 windowPos(
        (displaySize.x - windowSize.x) * 0.5f,
        (displaySize.y - windowSize.y) * 0.5f);
    const float panelTransition = UpdatePanelTransition();
    const float panelAlpha = 0.32f + panelTransition * 0.68f;
    const float panelLift = (1.0f - panelTransition) * 18.0f * scale;

    g_MenuBackdropState.Visible = true;
    g_MenuBackdropState.Pos = windowPos;
    g_MenuBackdropState.Size = windowSize;
    g_MenuBackdropState.Rounding = 30.0f * scale;
    g_MenuBackdropState.Opacity = ConfigManager::F("Misc.ThemeBackdropOpacity");
    g_MenuBackdropState.Tint = ConfigManager::Color("Misc.ThemeTint");
    g_MenuBackdropState.Accent = ConfigManager::Color("Misc.ThemeAccent");
    g_MenuBackdropState.Glow = ConfigManager::Color("Misc.ThemeGlow");
    g_MenuBackdropState.GlowStrength = ConfigManager::F("Misc.ThemeGlowStrength");
    g_MenuBackdropState.GlowSpread = ConfigManager::F("Misc.ThemeGlowSpread");

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 20.0f * scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 30.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin(
        "##main_menu_window",
        nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::SetWindowFontScale(1.16f);
        RenderChrome(ImGui::GetWindowPos(), ImGui::GetWindowSize());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 56.0f * scale);

        if (ImGui::BeginTable("##main_layout", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthFixed, 208.0f * scale);
            ImGui::TableSetupColumn("Primary", ImGuiTableColumnFlags_WidthStretch, 1.75f);
            ImGui::TableSetupColumn("Secondary", ImGuiTableColumnFlags_WidthStretch, 0.95f);

            ImGui::TableNextColumn();
            RenderSidebar();

            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + panelLift);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelAlpha);
            if (BeginSurface("##primary_pane", GetPanelTitle(g_CurrentPanel)))
            {
                switch (g_CurrentPanel)
                {
                case PanelId::Player: RenderPlayerSection(); break;
                case PanelId::Camera: RenderCameraSection(); break;
                case PanelId::Aimbot: RenderAimbotSection(); break;
                case PanelId::Weapon: RenderWeaponSection(); break;
                case PanelId::Esp: RenderEspPanel(); break;
                case PanelId::World: RenderWorldSection(); break;
                case PanelId::Misc: RenderMiscSection(); break;
                case PanelId::ThemeStudio: RenderThemeStudioSection(); break;
                case PanelId::Hotkeys:
                    HotkeyManager::RenderHotkeyTab();
                    break;
                case PanelId::About:
                    RenderAboutSection();
                    break;
                default:
                    break;
                }
            }
            EndSurface();
            ImGui::PopStyleVar();

            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + panelLift * 0.7f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.40f + panelTransition * 0.60f);
            RenderContextPane();
            ImGui::PopStyleVar();

            ImGui::EndTable();
        }
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

const GUI::MenuBackdropState& GUI::GetMenuBackdropState()
{
    return g_MenuBackdropState;
}
