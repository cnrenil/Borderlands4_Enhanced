#pragma once
#include "pch.h"
#include <string>
#include <vector>
#include <functional>

namespace HotkeyManager
{
    struct Hotkey {
        std::string Name;       // Key string in ConfigMap (e.g., "Aimbot.Key")
        std::string Label;      // Localization key (e.g., "AIMBOT_KEY")
        ImGuiKey DefaultKey;    // Default value
        std::function<void()> Callback; // Execution callback for one-shot press
        bool bIsBinding = false; 
    };

    void Initialize();
    void Update();
    
    // Register a hotkey with a name, localized label, default key, and optional callback
    void Register(const std::string& name, const std::string& label, ImGuiKey defaultKey, std::function<void()> callback = nullptr);
    
    // UI rendering for hotkeys tab
    void RenderHotkeyTab();

    extern std::vector<Hotkey> Hotkeys;
}
