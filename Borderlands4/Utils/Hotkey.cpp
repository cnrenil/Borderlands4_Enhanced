#include "pch.h"

extern std::atomic<bool> Cleaning;

namespace HotkeyManager
{
    std::vector<Hotkey> Hotkeys;

    namespace
    {
        bool IsPriorityHotkey(const Hotkey& hk)
        {
            return hk.Name == "Misc.MenuKey" || hk.Name == "Misc.UnhookKey";
        }

        int GetHotkeySortPriority(const Hotkey& hk)
        {
            if (hk.Name == "Misc.MenuKey")
                return 0;
            if (hk.Name == "Misc.UnhookKey")
                return 1;
            return 10;
        }

        bool IsValidHotkeyValue(int keyValue)
        {
            return keyValue == ImGuiKey_None || (keyValue >= ImGuiKey_NamedKey_BEGIN && keyValue < ImGuiKey_NamedKey_END);
        }

        ImGuiKey GetSanitizedHotkeyValue(const Hotkey& hk)
        {
            int& configuredKey = ConfigManager::I(hk.Name);
            if (IsValidHotkeyValue(configuredKey))
            {
                return static_cast<ImGuiKey>(configuredKey);
            }

            LOG_WARN("Hotkey", "Invalid key binding for %s: %d. Resetting to default %d.", hk.Name.c_str(), configuredKey, (int)hk.DefaultKey);
            configuredKey = static_cast<int>(hk.DefaultKey);
            return hk.DefaultKey;
        }

        void FormatHotkeyLabel(char* buffer, size_t bufferSize, const Hotkey& hk, ImGuiKey key)
        {
            if (hk.bIsBinding)
            {
                snprintf(buffer, bufferSize, "%s", Localization::T("PRESS_ANY_KEY"));
                return;
            }

            if (key == ImGuiKey_None)
            {
                snprintf(buffer, bufferSize, "%s", Localization::T("UNKNOWN_KEY"));
                return;
            }

            const char* keyName = ImGui::GetKeyName(key);
            if (!keyName || keyName[0] == '\0')
            {
                snprintf(buffer, bufferSize, "%s (%d)", Localization::T("UNKNOWN_KEY"), (int)key);
                return;
            }

            snprintf(buffer, bufferSize, "%s", keyName);
        }
    }

    void Register(const std::string& name, const std::string& label, ImGuiKey defaultKey, std::function<void()> callback, bool bIsHold)
    {
        // 1. Register in ConfigManager if not already there
        if (!ConfigManager::Exists(name)) {
            ConfigManager::Register(name, (int)defaultKey);
        }

        // 2. Add to our local list for the UI and execution
        Hotkeys.push_back({ name, label, defaultKey, callback, bIsHold, false });
    }

    void Initialize()
    {
        Hotkeys.clear();

        // Important controls should stay first in both data order and UI.
        Register("Misc.MenuKey", "MENU_KEY", ImGuiKey_Insert, []() {
            GUI::ShowMenu = !GUI::ShowMenu;
        });
        Register("Misc.UnhookKey", "UNHOOK_KEY", ImGuiKey_End, []() {
            Cleaning.store(true);
        });

        // Aimbot & Trigger: These are 'Hold' hotkeys. Their callbacks run continuously.
        Register("Aimbot.Key", "AIMBOT_KEY", ImGuiKey_MouseX2, []() { Cheats::AimbotHotkey(); }, true);
        Register("Trigger.Key", "TRIGGER_KEY", ImGuiKey_MouseX2, []() { Cheats::TriggerHotkey(); }, true);

        Register("Player.GodModeKey", "GODMODE_KEY", ImGuiKey_None, []() {
            Cheats::ToggleGodMode();
        });

        Register("Player.InfAmmoKey", "INF_AMMO_KEY", ImGuiKey_None, []() {
            Cheats::InfiniteAmmo();
        });

        Register("Misc.ThirdPersonKey", "TOGGLE_THIRDPERSON_KEY", ImGuiKey_F5, []() {
            ConfigManager::B("Player.ThirdPerson") = !ConfigManager::B("Player.ThirdPerson");
            if (ConfigManager::B("Player.ThirdPerson"))
            {
                ConfigManager::B("Player.Freecam") = false;
            }
        });
        Register("Misc.TeleportLootKey", "TELEPORT_LOOT_KEY", ImGuiKey_F6, []() {
            Cheats::TeleportLoot();
        });

#if BL4_DEBUG_BUILD
        Register("Misc.DumpKey", "DUMP_OBJECTS_KEY", ImGuiKey_F8, []() {
            Cheats::DumpObjects();
        });
#endif

        std::stable_sort(Hotkeys.begin(), Hotkeys.end(), [](const Hotkey& lhs, const Hotkey& rhs) {
            return GetHotkeySortPriority(lhs) < GetHotkeySortPriority(rhs);
        });

    }

    void Update()
    {
        // Don't process game-affecting hotkeys if loading, but allow menu toggle
		bool IsLoading = Utils::bIsLoading;
		bool IsInGame = Utils::bIsInGame;

        for (auto& hk : Hotkeys)
        {
            if (hk.Callback)
            {
                ImGuiKey currentKey = GetSanitizedHotkeyValue(hk);
                bool bIsMenuKey = (hk.Name == "Misc.MenuKey");
                bool bIsUnhookKey = (hk.Name == "Misc.UnhookKey");
                
                // Allow menu/unhook hotkeys even if gameplay state is unavailable.
				if ((IsLoading || !IsInGame) && !bIsMenuKey && !bIsUnhookKey) continue;

                if (currentKey >= ImGuiKey_NamedKey_BEGIN && currentKey < ImGuiKey_NamedKey_END)
                {
                    bool bShouldTrigger = hk.bIsHold ? ImGui::IsKeyDown(currentKey) : ImGui::IsKeyPressed(currentKey, false);

                    if (bShouldTrigger)
                    {
                        if (bIsMenuKey || bIsUnhookKey || !ImGui::GetIO().WantCaptureKeyboard)
                        {
                            if (!hk.bIsHold) {
                                LOG_DEBUG("Hotkey", "Triggered: %s (Key: %s)", hk.Label.c_str(), ImGui::GetKeyName(currentKey));
                            }
                            hk.Callback();
                        }
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
            const bool bPriorityHotkey = IsPriorityHotkey(hk);
            const bool bEmphasizePriorityHotkey = bPriorityHotkey && !ConfigManager::B("Misc.InitialMenuHintSeen");
            
            ImGui::Text("%s:", Localization::T(hk.Label));
            ImGui::SameLine(200);

            char buf[64]{};
            ImGuiKey currentKey = GetSanitizedHotkeyValue(hk);
            FormatHotkeyLabel(buf, sizeof(buf), hk, currentKey);

            if (bEmphasizePriorityHotkey)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68f, 0.20f, 0.18f, 0.96f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.26f, 0.22f, 0.98f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.92f, 0.34f, 0.24f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.98f, 0.54f, 0.42f, 0.95f));
            }
            if (ImGui::Button(buf, ImVec2(150, 0))) {
                hk.bIsBinding = true;
            }
            if (bEmphasizePriorityHotkey)
            {
                ImGui::PopStyleColor(4);
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
