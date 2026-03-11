#pragma once

namespace GUI
{
    extern bool ShowMenu;
    extern ImGuiKey TriggerBotKey;
    extern ImGuiKey ESPKey;
    extern ImGuiKey AimButton;

    void RenderMenu();
    void AddDefaultTooltip(const char* desc);
    void HostOnlyTooltip();
}


