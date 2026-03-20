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
        World,
        Aimbot,
        Weapon,
        Esp,
        Misc,
        Hotkeys,
        About,
        Count
    };

    struct TileRect
    {
        ImVec2 Pos;
        ImVec2 Size;
    };

    struct PanelLayoutState
    {
        int SlotIndex = 0;
        ImVec2 CurrentPos{};
        ImVec2 CurrentSize{};
        float Alpha = 0.0f;
        bool Initialized = false;
    };

    std::array<PanelLayoutState, static_cast<size_t>(PanelId::Count)> g_PanelLayouts = {{
        {0}, {4}, {1}, {5}, {2}, {6}, {3}, {7}
    }};
    int g_DraggingPanel = -1;
    int FindPanelBySlot(int slotIndex);

    ImVec2 LerpVec2(const ImVec2& from, const ImVec2& to, float t)
    {
        return ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
    }

    void DrawFloatingWindowChrome(const ImVec2& pos, const ImVec2& size, ImU32 accent, float alpha, bool compact = false)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = compact ? 20.0f : 26.0f;
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        const float pulse = 0.5f + 0.5f * std::sinf(static_cast<float>(ImGui::GetTime()) * 1.8f);
        const int shadowAlpha = static_cast<int>((compact ? 24.0f : 34.0f) * alpha);
        const int borderAlpha = static_cast<int>((compact ? 24.0f : 30.0f) * alpha);
        const int innerBorderAlpha = static_cast<int>((compact ? 10.0f : 14.0f) * alpha);
        const int glowAlpha = static_cast<int>((28.0f + pulse * 22.0f) * alpha);

        drawList->AddRectFilled(
            ImVec2(pos.x + 10.0f, pos.y + 14.0f),
            ImVec2(max.x + 10.0f, max.y + 14.0f),
            IM_COL32(0, 0, 0, shadowAlpha),
            rounding + 4.0f);

        drawList->AddRectFilledMultiColor(
            pos,
            max,
            IM_COL32(255, 255, 255, static_cast<int>((compact ? 16.0f : 22.0f) * alpha)),
            IM_COL32(255, 255, 255, static_cast<int>((compact ? 10.0f : 16.0f) * alpha)),
            IM_COL32(255, 255, 255, static_cast<int>((compact ? 6.0f : 12.0f) * alpha)),
            IM_COL32(255, 255, 255, static_cast<int>((compact ? 12.0f : 18.0f) * alpha)));

        drawList->AddRect(
            pos,
            max,
            IM_COL32(255, 255, 255, borderAlpha),
            rounding,
            0,
            1.0f);

        drawList->AddRect(
            ImVec2(pos.x + 1.0f, pos.y + 1.0f),
            ImVec2(max.x - 1.0f, max.y - 1.0f),
            IM_COL32(255, 255, 255, innerBorderAlpha),
            rounding - 1.0f,
            0,
            1.0f);

        drawList->AddLine(
            ImVec2(pos.x + 18.0f, pos.y + 18.0f),
            ImVec2(pos.x + size.x * (0.34f + pulse * 0.08f), pos.y + 18.0f),
            IM_COL32(255, 255, 255, glowAlpha),
            1.0f);

        drawList->AddLine(
            ImVec2(pos.x + 18.0f, pos.y + 48.0f),
            ImVec2(pos.x + size.x - 18.0f, pos.y + 48.0f),
            IM_COL32(255, 255, 255, static_cast<int>((compact ? 14.0f : 18.0f) * alpha)),
            1.0f);

        drawList->AddLine(
            ImVec2(pos.x + 18.0f, pos.y + 4.0f),
            ImVec2(pos.x + size.x * 0.42f, pos.y + 4.0f),
            IM_COL32(
                (accent >> IM_COL32_R_SHIFT) & 0xFF,
                (accent >> IM_COL32_G_SHIFT) & 0xFF,
                (accent >> IM_COL32_B_SHIFT) & 0xFF,
                static_cast<int>(((accent >> IM_COL32_A_SHIFT) & 0xFF) * alpha)),
            2.0f);
    }

    ImU32 GetOverlayAccentColor()
    {
        return IM_COL32(102, 214, 213, 220);
    }

    float GetMenuUiScale()
    {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float scaleX = displaySize.x / 1920.0f;
        const float scaleY = displaySize.y / 1080.0f;
        return std::clamp((std::min)(scaleX, scaleY), 0.64f, 1.28f);
    }

    std::array<TileRect, static_cast<size_t>(PanelId::Count)> BuildTileRects()
    {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float scale = GetMenuUiScale();
        std::array<TileRect, static_cast<size_t>(PanelId::Count)> rects{};
        const float margin = 24.0f * scale;
        const float gap = 18.0f * scale;
        const float safeWidth = (std::max)(displaySize.x - margin * 2.0f, 320.0f);
        const float safeHeight = (std::max)(displaySize.y - margin * 2.0f, 320.0f);

        if (displaySize.x >= 1680.0f && displaySize.y >= 900.0f)
        {
            const float usableWidth = safeWidth - gap * 3.0f;
            const float usableHeight = safeHeight - gap;
            const float totalUnits = 1.54f + 1.16f + 0.90f + 0.80f;
            const float unitWidth = usableWidth / totalUnits;
            const float columnWidths[4] = {
                unitWidth * 1.54f,
                unitWidth * 1.16f,
                unitWidth * 0.90f,
                unitWidth * 0.80f
            };
            const float topHeight = usableHeight * 0.60f;
            const float bottomHeight = usableHeight - topHeight;

            float x = margin;
            for (int col = 0; col < 4; ++col)
            {
                rects[col] = {
                    ImVec2(x, margin),
                    ImVec2(columnWidths[col], topHeight)
                };
                rects[col + 4] = {
                    ImVec2(x, margin + topHeight + gap),
                    ImVec2(columnWidths[col], bottomHeight)
                };
                x += columnWidths[col] + gap;
            }
            return rects;
        }

        if (displaySize.x >= 1280.0f)
        {
            const float usableWidth = safeWidth - gap * 2.0f;
            const float topHeight = safeHeight * 0.42f;
            const float middleHeight = safeHeight * 0.30f;
            const float bottomHeight = safeHeight - topHeight - middleHeight - gap * 2.0f;
            const float totalUnits = 1.30f + 1.00f + 0.82f;
            const float unitWidth = usableWidth / totalUnits;
            const float col0 = unitWidth * 1.30f;
            const float col1 = unitWidth * 1.00f;
            const float col2 = unitWidth * 0.82f;
            const float x0 = margin;
            const float x1 = x0 + col0 + gap;
            const float x2 = x1 + col1 + gap;
            const float y1 = margin + topHeight + gap;
            const float y2 = y1 + middleHeight + gap;

            rects[0] = { ImVec2(x0, margin), ImVec2(col0, topHeight) };
            rects[1] = { ImVec2(x1, margin), ImVec2(col1, topHeight) };
            rects[2] = { ImVec2(x2, margin), ImVec2(col2, topHeight) };
            rects[3] = { ImVec2(x0, y1), ImVec2(col0, middleHeight) };
            rects[4] = { ImVec2(x1, y1), ImVec2(col1, middleHeight) };
            rects[5] = { ImVec2(x2, y1), ImVec2(col2, middleHeight) };
            rects[6] = { ImVec2(x0, y2), ImVec2(col0 + gap + col1, bottomHeight) };
            rects[7] = { ImVec2(x2, y2), ImVec2(col2, bottomHeight) };
            return rects;
        }

        const float usableWidth = safeWidth - gap;
        const float totalUnits = displaySize.x >= 960.0f ? (1.14f + 0.86f) : 2.0f;
        const float leftWidth = usableWidth * ((displaySize.x >= 960.0f ? 1.14f : 1.0f) / totalUnits);
        const float rightWidth = usableWidth - leftWidth;
        const float rowHeight = (safeHeight - gap * 3.0f) / 4.0f;
        const float x0 = margin;
        const float x1 = margin + leftWidth + gap;

        for (int row = 0; row < 4; ++row)
        {
            const float y = margin + row * (rowHeight + gap);
            rects[row * 2] = {
                ImVec2(x0, y),
                ImVec2(leftWidth, rowHeight)
            };
            rects[row * 2 + 1] = {
                ImVec2(x1, y),
                ImVec2(rightWidth, rowHeight)
            };
        }
        return rects;
    }

    int FindNearestTile(const ImVec2& pos, const std::array<TileRect, static_cast<size_t>(PanelId::Count)>& rects)
    {
        int bestIndex = 0;
        float bestDistSq = FLT_MAX;
        for (size_t i = 0; i < rects.size(); ++i)
        {
            const ImVec2 delta(pos.x - rects[i].Pos.x, pos.y - rects[i].Pos.y);
            const float distSq = delta.x * delta.x + delta.y * delta.y;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestIndex = static_cast<int>(i);
            }
        }
        return bestIndex;
    }

    void DrawLayoutBackdrop(const std::array<TileRect, static_cast<size_t>(PanelId::Count)>& rects)
    {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const float time = static_cast<float>(ImGui::GetTime());
        const float pulse = 0.5f + 0.5f * std::sinf(time * 0.7f);
        const int hoveredSlot = (g_DraggingPanel >= 0) ? FindNearestTile(ImGui::GetMousePos(), rects) : -1;

        drawList->AddRectFilledMultiColor(
            ImVec2(0.0f, 0.0f),
            displaySize,
            IM_COL32(6, 10, 18, 48),
            IM_COL32(4, 8, 14, 26),
            IM_COL32(2, 5, 9, 34),
            IM_COL32(5, 9, 15, 40));

        for (size_t i = 0; i < rects.size(); ++i)
        {
            const TileRect& rect = rects[i];
            const bool occupied = FindPanelBySlot(static_cast<int>(i)) >= 0;
            const float intensity = occupied ? 1.0f : 0.75f;
            const bool hovered = hoveredSlot == static_cast<int>(i);
            const ImVec2 outerMin(rect.Pos.x - 5.0f, rect.Pos.y - 5.0f);
            const ImVec2 outerMax(rect.Pos.x + rect.Size.x + 5.0f, rect.Pos.y + rect.Size.y + 5.0f);

            drawList->AddRectFilled(
                outerMin,
                outerMax,
                hovered
                    ? IM_COL32(102, 214, 213, static_cast<int>(18.0f + pulse * 16.0f))
                    : IM_COL32(255, 255, 255, static_cast<int>((8.0f + pulse * 5.0f) * intensity)),
                24.0f);
            drawList->AddRect(
                outerMin,
                outerMax,
                hovered
                    ? IM_COL32(102, 214, 213, static_cast<int>(48.0f + pulse * 40.0f))
                    : IM_COL32(255, 255, 255, static_cast<int>((14.0f + pulse * 8.0f) * intensity)),
                24.0f,
                0,
                hovered ? 2.0f : 1.0f);
        }
    }

    int FindPanelBySlot(int slotIndex)
    {
        for (size_t i = 0; i < g_PanelLayouts.size(); ++i)
        {
            if (g_PanelLayouts[i].SlotIndex == slotIndex)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool BeginTiledPanel(
        PanelId panelId,
        const char* title,
        const std::array<TileRect, static_cast<size_t>(PanelId::Count)>& rects,
        float fontScale)
    {
        const int panelIndex = static_cast<int>(panelId);
        PanelLayoutState& layout = g_PanelLayouts[panelIndex];
        layout.SlotIndex = (std::clamp)(layout.SlotIndex, 0, static_cast<int>(rects.size()) - 1);
        const TileRect& rect = rects[layout.SlotIndex];
        const float deltaTime = (std::max)(ImGui::GetIO().DeltaTime, 1.0f / 240.0f);
        const float motionAlpha = 1.0f - std::exp(-10.0f * deltaTime);
        const float fadeAlpha = 1.0f - std::exp(-7.0f * deltaTime);

        if (!layout.Initialized)
        {
            layout.CurrentPos = rect.Pos;
            layout.CurrentSize = rect.Size;
            layout.Alpha = 0.0f;
            layout.Initialized = true;
        }

        const bool allowFreeDragThisFrame = (g_DraggingPanel == panelIndex);
        if (!allowFreeDragThisFrame)
        {
            layout.CurrentPos = LerpVec2(layout.CurrentPos, rect.Pos, motionAlpha);
            layout.CurrentSize = LerpVec2(layout.CurrentSize, rect.Size, motionAlpha);
        }
        layout.Alpha += (1.0f - layout.Alpha) * fadeAlpha;

        const GUI::ThemeDefinition* currentTheme = GUI::ThemeManager::GetThemeByIndex(ConfigManager::I("Misc.Theme"));
        const bool useGlassPanelOverrides = currentTheme && currentTheme->Id == "ocean_glass";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float panelFrameRounding = useGlassPanelOverrides
            ? (16.0f * fontScale)
            : style.FrameRounding;
        const float panelChildRounding = useGlassPanelOverrides
            ? (18.0f * fontScale)
            : style.ChildRounding;

        if (useGlassPanelOverrides)
            ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::SetNextWindowPos(layout.CurrentPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(layout.CurrentSize, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * fontScale, 16.0f * fontScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, panelFrameRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, panelChildRounding);
        int pushedColors = 0;
        if (useGlassPanelOverrides)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.00f, 1.00f, 1.00f, 0.035f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.00f, 1.00f, 1.00f, 0.025f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.00f, 1.00f, 1.00f, 0.045f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.00f, 1.00f, 1.00f, 0.065f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.00f, 1.00f, 1.00f, 0.03f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 1.00f, 1.00f, 0.05f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 1.00f, 1.00f, 0.07f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.00f, 1.00f, 1.00f, 0.02f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.00f, 1.00f, 1.00f, 0.045f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.00f, 1.00f, 1.00f, 0.065f));
            pushedColors = 10;
        }

        const bool opened = ImGui::Begin(
            title,
            nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings);
        if (pushedColors > 0)
            ImGui::PopStyleColor(pushedColors);
        ImGui::PopStyleVar(4);

        if (allowFreeDragThisFrame)
        {
            layout.CurrentPos = ImGui::GetWindowPos();
            layout.CurrentSize = ImGui::GetWindowSize();
        }

        ImGui::SetWindowFontScale(fontScale);
        DrawFloatingWindowChrome(ImGui::GetWindowPos(), ImGui::GetWindowSize(), GetOverlayAccentColor(), layout.Alpha, true);

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            g_DraggingPanel = panelIndex;
        }

        if (g_DraggingPanel == panelIndex && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const int targetSlot = FindNearestTile(ImGui::GetWindowPos(), rects);
            const int occupyingPanel = FindPanelBySlot(targetSlot);
            if (occupyingPanel >= 0 && occupyingPanel != panelIndex)
            {
                std::swap(layout.SlotIndex, g_PanelLayouts[occupyingPanel].SlotIndex);
            }
            else
            {
                layout.SlotIndex = targetSlot;
            }
            g_DraggingPanel = -1;
        }

        return opened;
    }

    void SectionHeader(const char* label)
    {
        ImGui::Spacing();
        ImGui::SeparatorText(label);
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
        ImGui::SeparatorText(Localization::T("TAB_ABOUT"));
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

        ImGui::SeparatorText(Localization::T("TAB_PLAYER"));
        SectionHeader(Localization::T("CORE_FEATURES"));
        ImGui::Checkbox(Localization::T("ESP"), &B("Player.ESP"));
        ImGui::Checkbox(Localization::T("AIMBOT"), &B("Aimbot.Enabled"));
        ImGui::Checkbox(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
        ImGui::Checkbox(Localization::T("INF_GRENADES"), &B("Player.InfGrenades"));
        ImGui::Checkbox(Localization::T("GODMODE"), &B("Player.GodMode"));
        ImGui::Checkbox(Localization::T("DEMIGOD"), &B("Player.Demigod"));
        ImGui::Checkbox(Localization::T("NO_TARGET"), &B("Player.NoTarget"));

        SectionHeader(Localization::T("MOVEMENT"));
        ImGui::Checkbox(Localization::T("SPEED_HACK"), &B("Player.SpeedEnabled"));
        if (B("Player.SpeedEnabled"))
        {
            ImGui::SliderFloat(Localization::T("SPEED_VALUE"), &F("Player.Speed"), 1.0f, 10.0f, "%.1f");
        }

        ImGui::Checkbox(Localization::T("FLIGHT"), &B("Player.Flight"));
        if (B("Player.Flight"))
        {
            ImGui::SliderFloat(Localization::T("FLIGHT_SPEED"), &F("Player.FlightSpeed"), 1.0f, 20.0f, "%.1f");
        }
        ImGui::Checkbox(Localization::T("INF_VEHICLE_BOOST"), &B("Player.InfVehicleBoost"));
        ImGui::Checkbox(Localization::T("VEHICLE_SPEED_HACK"), &B("Player.VehicleSpeedEnabled"));
        if (B("Player.VehicleSpeedEnabled"))
        {
            ImGui::SliderFloat(Localization::T("VEHICLE_SPEED_VALUE"), &F("Player.VehicleSpeed"), 1.0f, 20.0f, "%.1f");
        }
        ImGui::Checkbox(Localization::T("INF_GLIDE_STAMINA"), &B("Player.InfGlideStamina"));

        SectionHeader(Localization::T("CAMERA_SETTINGS"));
        const bool bTPEnabled = B("Player.ThirdPerson");
        const bool bFreecamEnabled = B("Player.Freecam");

        if (bFreecamEnabled)
        {
            bool bFreecam = B("Player.Freecam");
            if (ImGui::Checkbox(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
            ImGui::SameLine();
            ImGui::Checkbox(Localization::T("FREECAM_BLOCK_INPUT"), &B("Misc.FreecamBlockInput"));
        }
        else if (bTPEnabled)
        {
            bool bTP = B("Player.ThirdPerson");
            if (ImGui::Checkbox(Localization::T("THIRD_PERSON"), &bTP)) Cheats::ToggleThirdPerson();
            ImGui::SameLine();
            ImGui::Checkbox(Localization::T("THIRD_PERSON_CENTERED"), &B("Misc.ThirdPersonCentered"));

            ImGui::Indent();
            bool bOTS = B("Player.OverShoulder");
            if (ImGui::Checkbox(Localization::T("OVER_SHOULDER"), &bOTS))
            {
                B("Player.OverShoulder") = bOTS;
            }
            if (B("Player.OverShoulder"))
            {
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_X"), &F("Misc.OTS_X"), -500.0f, 500.0f);
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_Y"), &F("Misc.OTS_Y"), -200.0f, 200.0f);
                ImGui::SliderFloat(Localization::T("OTS_OFFSET_Z"), &F("Misc.OTS_Z"), -200.0f, 200.0f);
                ImGui::Checkbox(Localization::T("OTS_ADS_CAMERA_OVERRIDE"), &B("Misc.OTSADSOverride"));
                if (B("Misc.OTSADSOverride"))
                {
                    ImGui::Checkbox(Localization::T("ADS_FIRST_PERSON"), &B("Misc.OTSADSFirstPerson"));
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
                ImGui::Checkbox(Localization::T("ADS_FIRST_PERSON"), &B("Misc.ThirdPersonADSFirstPerson"));
            }
            ImGui::Unindent();
        }
        else
        {
            bool bTP = B("Player.ThirdPerson");
            if (ImGui::Checkbox(Localization::T("THIRD_PERSON"), &bTP))
            {
                Cheats::ToggleThirdPerson();
            }

            bool bFreecam = B("Player.Freecam");
            if (ImGui::Checkbox(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
        }

        SectionHeader(Localization::T("PLAYER_PROGRESSION"));
        static int xpAmount = 1;
        ImGui::InputInt(Localization::T("EXPERIENCE_LEVEL"), &xpAmount);
        if (ImGui::Button(Localization::T("SET_EXPERIENCE_LEVEL")))
        {
            Cheats::SetExperienceLevel(xpAmount);
        }
    }

    void RenderWorldSection()
    {
        using namespace ConfigManager;

        ImGui::SeparatorText(Localization::T("TAB_WORLD"));
        SectionHeader(Localization::T("WORLD_ACTIONS"));
        if (ImGui::Button(Localization::T("KILL_ENEMIES"))) Cheats::KillEnemies();
        if (ImGui::Button(Localization::T("CLEAR_GROUND_ITEMS"))) Cheats::ClearGroundItems();
        if (ImGui::Button(Localization::T("TELEPORT_LOOT"))) Cheats::TeleportLoot();
        if (ImGui::Button(Localization::T("SPAWN_ITEMS"))) Cheats::SpawnItems();
        if (ImGui::Button(Localization::T("GIVE_5_LEVELS"))) Cheats::GiveLevels();

        SectionHeader(Localization::T("WORLD_SIMULATION"));
        if (ImGui::Checkbox(Localization::T("PLAYERS_ONLY"), &B("Player.PlayersOnly"))) Cheats::TogglePlayersOnly();

        if (ImGui::SliderFloat(Localization::T("GAME_SPEED"), &F("Player.GameSpeed"), 0.1f, 10.0f))
            Cheats::SetGameSpeed(F("Player.GameSpeed"));

        SectionHeader(Localization::T("TELEPORT_SETTINGS"));
        ImGui::Checkbox(Localization::T("MAP_TELEPORT"), &B("Misc.MapTeleport"));
        GUI::AddDefaultTooltip("Quickly make and remove a pin on the map to teleport to that location.");
        ImGui::SliderFloat(Localization::T("MAP_TELEPORT_WINDOW"), &F("Misc.MapTPWindow"), 0.5f, 5.0f);

        SectionHeader(Localization::T("CURRENCY_SETTINGS"));
        static int CurrencyAmount = 1000;
        ImGui::InputInt("##Amount", &CurrencyAmount);
        if (ImGui::Button(Localization::T("CASH"))) Cheats::AddCurrency("Cash", CurrencyAmount);
        ImGui::SameLine();
        if (ImGui::Button(Localization::T("ERIDIUM"))) Cheats::AddCurrency("eridium", CurrencyAmount);
        ImGui::SameLine();
        if (ImGui::Button(Localization::T("VC_TICKETS"))) Cheats::AddCurrency("VaultCard01_Tokens", CurrencyAmount);
    }

    void RenderAimbotSection()
    {
        using namespace ConfigManager;

        ImGui::SeparatorText(Localization::T("AIMBOT"));
        SectionHeader(Localization::T("STANDARD_AIMBOT_SETTINGS"));
        ImGui::Checkbox(Localization::T("REQUIRE_LOS"), &B("Aimbot.LOS"));
        ImGui::Checkbox(Localization::T("REQUIRE_KEY_HELD"), &B("Aimbot.RequireKeyHeld"));
        ImGui::Checkbox(Localization::T("TARGET_ALL"), &B("Aimbot.TargetAll"));
        ImGui::Checkbox(Localization::T("SILENT_AIM"), &B("Aimbot.Silent"));
        if (B("Aimbot.Silent"))
        {
            ImGui::Indent();
            ImGui::Checkbox(Localization::T("USE_NATIVE_PROJECTILE_HOOK"), &B("Aimbot.NativeProjectileHook"));
            ImGui::Checkbox(Localization::T("MAGIC_BULLETS"), &B("Aimbot.Magic"));
            ImGui::Unindent();
        }
        ImGui::Checkbox(Localization::T("SMOOTH_AIM"), &B("Aimbot.Smooth"));
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
        ImGui::Checkbox(Localization::T("DRAW_FOV"), &B("Aimbot.DrawFOV"));
        if (B("Aimbot.DrawFOV"))
            ImGui::SliderFloat(Localization::T("FOV_LINE_THICKNESS"), &F("Aimbot.FOVThickness"), 1.0f, 6.0f, "%.1f");
        ImGui::Checkbox(Localization::T("DRAW_ARROW"), &B("Aimbot.DrawArrow"));
        if (B("Aimbot.DrawArrow"))
            ImGui::SliderFloat(Localization::T("ARROW_LINE_THICKNESS"), &F("Aimbot.ArrowThickness"), 1.0f, 6.0f, "%.1f");

        SectionHeader(Localization::T("TRIGGERBOT"));
        ImGui::Checkbox(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));
        if (B("Trigger.Enabled"))
        {
            ImGui::Indent();
            ImGui::Checkbox(Localization::T("REQUIRE_KEY_HELD"), &B("Trigger.RequireKeyHeld"));
            ImGui::Checkbox(Localization::T("TARGET_ALL"), &B("Trigger.TargetAll"));
            ImGui::Unindent();
        }
    }

    void RenderWeaponSection()
    {
        using namespace ConfigManager;

        ImGui::SeparatorText(Localization::T("TAB_WEAPON"));
        SectionHeader(Localization::T("BALLISTICS"));
        ImGui::Checkbox(Localization::T("INSTANT_HIT"), &B("Weapon.InstantHit"));
        GUI::AddDefaultTooltip("Increases bullet speed to effectively hit targets instantly.");
        if (B("Weapon.InstantHit"))
        {
            ImGui::SliderFloat(Localization::T("PROJECTILE_SPEED"), &F("Weapon.ProjectileSpeedMultiplier"), 1.0f, 9999.0f);
        }

        SectionHeader(Localization::T("FIRING"));
        ImGui::Checkbox(Localization::T("RAPID_FIRE"), &B("Weapon.RapidFire"));
        if (B("Weapon.RapidFire"))
        {
            ImGui::SliderFloat(Localization::T("FIRE_RATE_MODIFIER"), &F("Weapon.FireRate"), 0.1f, 10.0f, "%.1f");
        }

        SectionHeader(Localization::T("STABILITY"));
        ImGui::Checkbox(Localization::T("NO_RECOIL"), &B("Weapon.NoRecoil"));
        if (B("Weapon.NoRecoil"))
        {
            ImGui::SliderFloat(Localization::T("RECOIL_REDUCTION"), &F("Weapon.RecoilReduction"), 0.0f, 1.0f);
        }
        ImGui::Checkbox(Localization::T("NO_SPREAD"), &B("Weapon.NoSpread"));
        ImGui::Checkbox(Localization::T("NO_SWAY"), &B("Weapon.NoSway"));

        SectionHeader(Localization::T("AMMO_HANDLING"));
        ImGui::Checkbox(Localization::T("INSTANT_RELOAD"), &B("Weapon.InstantReload"));
        GUI::HostOnlyTooltip();
        ImGui::Checkbox(Localization::T("INSTANT_SWAP"), &B("Weapon.InstantSwap"));
        GUI::HostOnlyTooltip();
        ImGui::Checkbox(Localization::T("NO_AMMO_CONSUME"), &B("Weapon.NoAmmoConsume"));
        GUI::HostOnlyTooltip();
    }

    void RenderMiscSection()
    {
        using namespace ConfigManager;

        ImGui::SeparatorText(Localization::T("TAB_CONFIG"));

        SectionHeader(Localization::T("GENERAL_SETTINGS"));
        static const char* HostLangs[] = { "English", "简体中文" };
        int& CurrentLangIdx = ConfigManager::I("Misc.Language");
        CurrentLangIdx = (std::clamp)(CurrentLangIdx, 0, IM_ARRAYSIZE(HostLangs) - 1);
        if (ImGui::Combo(Localization::T("LANGUAGE"), &CurrentLangIdx, HostLangs, IM_ARRAYSIZE(HostLangs)))
        {
            Localization::CurrentLanguage = (Language)CurrentLangIdx;
        }

        int& themeIndex = ConfigManager::I("Misc.Theme");
        themeIndex = GUI::ThemeManager::ClampThemeIndex(themeIndex);
        if (ImGui::BeginCombo(Localization::T("THEME"), GUI::ThemeManager::GetThemeDisplayName(themeIndex)))
        {
            const int themeCount = GUI::ThemeManager::GetThemeCount();
            for (int i = 0; i < themeCount; ++i)
            {
                const bool selected = (themeIndex == i);
                if (ImGui::Selectable(GUI::ThemeManager::GetThemeDisplayName(i), selected))
                {
                    themeIndex = i;
                    GUI::ThemeManager::ApplyByIndex(i);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        SectionHeader(Localization::T("VIEW_SETTINGS"));
        ImGui::Checkbox(Localization::T("SHOW_ACTIVE_FEATURES"), &B("Misc.RenderOptions"));
        if (ImGui::Checkbox(Localization::T("ENABLE_FOV_CHANGER"), &B("Misc.EnableFOV")))
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

        ImGui::Checkbox(Localization::T("ENABLE_VIEWMODEL_FOV"), &B("Misc.EnableViewModelFOV"));
        if (B("Misc.EnableViewModelFOV"))
        {
            ImGui::SliderFloat(Localization::T("VIEWMODEL_FOV_VALUE"), &F("Misc.ViewModelFOV"), 60.0f, 150.0f);
        }

        ImGui::Checkbox(Localization::T("DISABLE_VOLUMETRIC_CLOUDS"), &B("Misc.DisableVolumetricClouds"));

        SectionHeader(Localization::T("RETICLE_SETTINGS"));
        ImGui::Checkbox(Localization::T("ENABLE_RETICLE"), &B("Misc.Reticle"));
        if (B("Misc.Reticle"))
        {
            ImGui::Checkbox(Localization::T("RETICLE_CROSSHAIR"), &B("Misc.CrossReticle"));
            ImGui::SliderFloat(Localization::T("RETICLE_SIZE"), &F("Misc.ReticleSize"), 2.0f, 30.0f, "%.1f");
            ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_X"), &Vec2("Misc.ReticlePosition").x, -200.0f, 200.0f, "%.0f");
            ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_Y"), &Vec2("Misc.ReticlePosition").y, -200.0f, 200.0f, "%.0f");
            ImGui::ColorEdit4(Localization::T("RETICLE_COLOR"), (float*)&Color("Misc.ReticleColor"), ImGuiColorEditFlags_NoInputs);
        }

#if BL4_DEBUG_BUILD
        SectionHeader(Localization::T("DEBUG"));
        if (ImGui::TreeNode(Localization::T("DEBUG")))
        {
            ImGui::Checkbox(Localization::T("ENABLE_EVENT_DEBUG_LOGS"), &B("Misc.Debug"));
            ImGui::Checkbox(Localization::T("ENABLE_PING_DUMP"), &B("Misc.PingDump"));

            bool bRecording = Logger::IsRecording();
            if (ImGui::Checkbox(Localization::T("ENABLE_EVENT_RECORDING"), &bRecording))
            {
                if (bRecording) Logger::StartRecording();
                else Logger::StopRecording();
            }

            if (ImGui::Button(Localization::T("DUMP_GOBJECTS"))) Cheats::DumpObjects();
            ImGui::TreePop();
        }
#endif

        SectionHeader(Localization::T("CONFIG_ACTIONS"));
        if (ImGui::Button(Localization::T("SAVE_SETTINGS")))
            ConfigManager::SaveSettings();
        ImGui::SameLine();
        if (ImGui::Button(Localization::T("LOAD_SETTINGS")))
            ConfigManager::LoadSettings();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        ImGui::TextWrapped(Localization::T("VOLATILE_HINT"));
        ImGui::PopStyleColor();
    }

    void RenderEspPanel()
    {
        using namespace ConfigManager;
        SectionHeader(Localization::T("ENEMY_ESP"));
        ImGui::Checkbox(Localization::T("SHOW_TEAM"), &B("ESP.ShowTeam"));
        ImGui::Checkbox(Localization::T("SHOW_BOX"), &B("ESP.ShowBox"));
        ImGui::Checkbox(Localization::T("SHOW_DISTANCE"), &B("ESP.ShowEnemyDistance"));
        ImGui::Checkbox(Localization::T("SHOW_BONES"), &B("ESP.Bones"));
        ImGui::Checkbox(Localization::T("SHOW_NAME"), &B("ESP.ShowEnemyName"));
        ImGui::Checkbox(Localization::T("SHOW_ENEMY_INDICATOR"), &B("ESP.ShowEnemyIndicator"));

        SectionHeader(Localization::T("TRACER_SETTINGS"));
        ImGui::Checkbox(Localization::T("SHOW_BULLET_TRACERS"), &B("ESP.BulletTracers"));
        if (B("ESP.BulletTracers"))
        {
            ImGui::Checkbox(Localization::T("TRACER_RAINBOW"), &B("ESP.TracerRainbow"));
            ImGui::SliderFloat(Localization::T("TRACER_DURATION"), &F("ESP.TracerDuration"), 0.1f, 8.0f, "%.1f s");
            if (!B("ESP.TracerRainbow"))
            {
                ImGui::ColorEdit4(Localization::T("TRACER_COLOR"), (float*)&Color("ESP.TracerColor"), ImGuiColorEditFlags_NoInputs);
            }
        }

        SectionHeader(Localization::T("LOOT_INTERACTIVES"));
        ImGui::Checkbox(Localization::T("SHOW_LOOT_NAME"), &B("ESP.ShowLootName"));
        if (B("ESP.ShowLootName"))
        {
            ImGui::SliderFloat(Localization::T("LOOT_MAX_DISTANCE"), &F("ESP.LootMaxDistance"), 10.0f, 1000.0f, "%.0f");
            ImGui::ColorEdit4(Localization::T("LOOT_COLOR"), (float*)&Color("ESP.LootColor"), ImGuiColorEditFlags_NoInputs);
        }

        ImGui::Checkbox(Localization::T("SHOW_INTERACTIVES"), &B("ESP.ShowInteractives"));
        if (B("ESP.ShowInteractives"))
        {
            ImGui::SliderFloat(Localization::T("INTERACTIVE_MAX_DISTANCE"), &F("ESP.InteractiveMaxDistance"), 10.0f, 1000.0f, "%.0f");
            ImGui::ColorEdit4(Localization::T("INTERACTIVE_COLOR"), (float*)&Color("ESP.InteractiveColor"), ImGuiColorEditFlags_NoInputs);
        }

        SectionHeader(Localization::T("COLOR_SETTINGS"));
        ImGui::ColorEdit4(Localization::T("ENEMY_COLOR"), (float*)&Color("ESP.EnemyColor"), ImGuiColorEditFlags_NoInputs);
        if (B("ESP.ShowTeam"))
        {
            ImGui::ColorEdit4(Localization::T("TEAM_COLOR"), (float*)&Color("ESP.TeamColor"), ImGuiColorEditFlags_NoInputs);
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
	if (!ShowMenu) return;

    const auto rects = BuildTileRects();
    const float fontScale = GetMenuUiScale();
    DrawLayoutBackdrop(rects);

    if (BeginTiledPanel(PanelId::Player, Localization::T("TAB_PLAYER"), rects, fontScale))
    {
        RenderPlayerSection();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::Aimbot, Localization::T("AIMBOT"), rects, fontScale))
    {
        RenderAimbotSection();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::Esp, Localization::T("ESP_SETTINGS"), rects, fontScale))
    {
        RenderEspPanel();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::Hotkeys, Localization::T("TAB_HOTKEYS"), rects, fontScale))
    {
        ImGui::SeparatorText(Localization::T("TAB_HOTKEYS"));
        HotkeyManager::RenderHotkeyTab();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::World, Localization::T("TAB_WORLD"), rects, fontScale))
    {
        RenderWorldSection();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::Weapon, Localization::T("TAB_WEAPON"), rects, fontScale))
    {
        RenderWeaponSection();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::Misc, Localization::T("TAB_CONFIG"), rects, fontScale))
    {
        RenderMiscSection();
    }
    ImGui::End();

    if (BeginTiledPanel(PanelId::About, Localization::T("TAB_ABOUT"), rects, fontScale))
    {
        RenderAboutSection();
    }
    ImGui::End();
}
