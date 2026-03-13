#include "pch.h"


namespace HotkeyManager
{
    std::vector<Hotkey> Hotkeys;

    void Register(const std::string& name, const std::string& label, ImGuiKey defaultKey, std::function<void()> callback)
    {
        // 1. Register in ConfigManager if not already there
        if (!ConfigManager::Exists(name)) {
            ConfigManager::Register(name, (int)defaultKey);
        }

        // 2. Add to our local list for the UI and execution
        Hotkeys.push_back({ name, label, defaultKey, callback, false });
    }

    void Initialize()
    {
        Hotkeys.clear();
        
        // Aimbot & Trigger: No callback here as they are 'held' logic handled in their own loops
        Register("Aimbot.Key", "AIMBOT_KEY", ImGuiKey_MouseX2);
        Register("Trigger.Key", "TRIGGER_KEY", ImGuiKey_MouseX2);
        
        // One-shot toggles with callbacks
        Register("Misc.MenuKey", "MENU_KEY", ImGuiKey_Insert, []() {
            GUI::ShowMenu = !GUI::ShowMenu;
            ImGui::GetIO().MouseDrawCursor = GUI::ShowMenu;
        });

        Register("Misc.ThirdPersonKey", "TOGGLE_THIRDPERSON_KEY", ImGuiKey_F5, []() {
            Cheats::ToggleThirdPerson();
        });

        Register("Misc.DumpKey", "DUMP_OBJECTS_KEY", ImGuiKey_F8, []() {
            Cheats::DumpObjects();
        });
    }

    void Update()
    {
        // Don't process game-affecting hotkeys if loading, but allow menu toggle
        bool IsLoading = Utils::bIsLoading;

        for (auto& hk : Hotkeys)
        {
            if (hk.Callback)
            {
                ImGuiKey currentKey = (ImGuiKey)ConfigManager::I(hk.Name);
                bool bIsMenuKey = (hk.Name == "Misc.MenuKey");
                
                // If loading, only allow the menu key
                if (IsLoading && !bIsMenuKey) continue;

                if (ImGui::IsKeyPressed(currentKey, false))
                {
                    if (bIsMenuKey || !ImGui::GetIO().WantCaptureKeyboard)
                    {
                        LOG_INFO("Hotkey", "Triggered: %s (Key: %s)", hk.Label.c_str(), ImGui::GetKeyName(currentKey));
                        hk.Callback();
                    }
                }
            }
        }
    }

    void RenderHotkeyTab()
    {
        ImGui::TextDisabled(Localization::T("HOTKEY_TAB_HELP"));
        ImGui::Separator();

        for (auto& hk : Hotkeys)
        {
            ImGui::PushID(hk.Name.c_str());
            
            ImGui::Text("%s:", Localization::T(hk.Label));
            ImGui::SameLine(200);

            char buf[64];
            ImGuiKey currentKey = (ImGuiKey)ConfigManager::I(hk.Name);
            
            if (hk.bIsBinding) {
                strcpy(buf, Localization::T("PRESS_ANY_KEY"));
            } else {
                const char* keyName = ImGui::GetKeyName(currentKey);
                if (!keyName || keyName[0] == '\0') {
                    sprintf(buf, "%s (%d)", Localization::T("UNKNOWN_KEY"), (int)currentKey);
                } else {
                    strcpy(buf, keyName);
                }
            }

            if (ImGui::Button(buf, ImVec2(150, 0))) {
                hk.bIsBinding = true;
            }

            if (hk.bIsBinding) {
                // Poll for any key press
                for (int n = ImGuiKey_NamedKey_BEGIN; n < ImGuiKey_NamedKey_END; n++) {
                    if (ImGui::IsKeyPressed((ImGuiKey)n)) {
                        ConfigManager::I(hk.Name) = n;
                        hk.bIsBinding = false;
                        break;
                    }
                }
                
                // Check common mouse keys specifically if not captured by NamedKey loop
                if (ImGui::IsMouseClicked(0)) { ConfigManager::I(hk.Name) = ImGuiKey_MouseLeft; hk.bIsBinding = false; }
                if (ImGui::IsMouseClicked(1)) { ConfigManager::I(hk.Name) = ImGuiKey_MouseRight; hk.bIsBinding = false; }
                if (ImGui::IsMouseClicked(2)) { ConfigManager::I(hk.Name) = ImGuiKey_MouseMiddle; hk.bIsBinding = false; }
                if (ImGui::IsMouseClicked(3)) { ConfigManager::I(hk.Name) = ImGuiKey_MouseX1; hk.bIsBinding = false; }
                if (ImGui::IsMouseClicked(4)) { ConfigManager::I(hk.Name) = ImGuiKey_MouseX2; hk.bIsBinding = false; }
                
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    hk.bIsBinding = false;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(Localization::T("RESET"))) {
                ConfigManager::I(hk.Name) = (int)hk.DefaultKey;
            }

            ImGui::PopID();
            ImGui::Separator();
        }
    }
}
