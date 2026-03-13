#include "pch.h"


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
	{"Head", BoneList.HeadBone},
	{"Neck", BoneList.NeckBone},
	{"Chest", BoneList.ChestBone},
	{"Stomach", BoneList.StomachBone},
	{"Pelvis", BoneList.PelvisBone}
};

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

	ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
	
	if (ImGui::Begin(Localization::T("WINDOW_TITLE"), nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::SeparatorText(Localization::T("HELLO_GREETING"));

		if (ImGui::BeginTabBar("MainTabBar"))
		{
			if (ImGui::BeginTabItem(Localization::T("TAB_ABOUT")))
			{
				static const char* HostLangs[] = { "English", "简体中文" };
				int& CurrentLangIdx = ConfigManager::I("Misc.Language");
				if (ImGui::Combo(Localization::T("LANGUAGE"), &CurrentLangIdx, HostLangs, IM_ARRAYSIZE(HostLangs)))
				{
					Localization::CurrentLanguage = (Language)CurrentLangIdx;
				}

				ImGui::Separator();

				ImGui::Text(Localization::T("MENU_TITLE"));
				ImGui::Text(Localization::T("VERSION_D_D_D"), MAJORVERSION, MINORVERSION, PATCHVERSION);

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

				ImGui::EndTabItem();
			}

            if (ImGui::BeginTabItem(Localization::T("TAB_PLAYER")))
            {
                using namespace ConfigManager;
                ImGui::Checkbox(Localization::T("ESP"), &B("Player.ESP"));
                ImGui::Checkbox(Localization::T("AIMBOT"), &B("Aimbot.Enabled"));
                if (ImGui::Checkbox(Localization::T("SILENT_AIM"), &B("SilentAim.Enabled"))) { if (B("SilentAim.Enabled")) B("Aimbot.Enabled") = false; }
                ImGui::Checkbox(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
                ImGui::Checkbox(Localization::T("GODMODE"), &B("Player.GodMode"));
                ImGui::Checkbox(Localization::T("DEMIGOD"), &B("Player.Demigod"));
                ImGui::Checkbox(Localization::T("NO_TARGET"), &B("Player.NoTarget"));
                
                ImGui::SeparatorText(Localization::T("MOVEMENT"));
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
                
                const bool bTPEnabled = B("Player.ThirdPerson");
                const bool bOTSEnabled = B("Player.OverShoulder");
                const bool bFreecamEnabled = B("Player.Freecam");

                if (bFreecamEnabled)
                {
                    bool bFreecam = B("Player.Freecam");
                    if (ImGui::Checkbox(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
                    ImGui::SameLine();
                    ImGui::Checkbox(Localization::T("FREECAM_BLOCK_INPUT"), &B("Misc.FreecamBlockInput"));
                }
                else if (bOTSEnabled)
                {
                    bool bOTS = B("Player.OverShoulder");
                    if (ImGui::Checkbox(Localization::T("OVER_SHOULDER"), &bOTS))
                    {
                        B("Player.OverShoulder") = bOTS;
                    }
                    ImGui::Indent();
                    ImGui::SliderFloat(Localization::T("OTS_OFFSET_X"), &F("Misc.OTS_X"), -500.0f, 500.0f);
                    ImGui::SliderFloat(Localization::T("OTS_OFFSET_Y"), &F("Misc.OTS_Y"), -200.0f, 200.0f);
                    ImGui::SliderFloat(Localization::T("OTS_OFFSET_Z"), &F("Misc.OTS_Z"), -200.0f, 200.0f);
                    ImGui::Checkbox(Localization::T("OTS_ADS_FOV_BOOST"), &B("Misc.OTSADSFOVBoost"));
                    if (B("Misc.OTSADSFOVBoost"))
                    {
                        ImGui::SliderFloat(Localization::T("OTS_ADS_FOV_SCALE"), &F("Misc.OTSADSFOVScale"), 0.2f, 3.0f, "%.2f");
                    }
                    ImGui::Unindent();
                }
                else if (bTPEnabled)
                {
                    bool bTP = B("Player.ThirdPerson");
                    if (ImGui::Checkbox(Localization::T("THIRD_PERSON"), &bTP)) Cheats::ToggleThirdPerson();
                    ImGui::SameLine();
                    ImGui::Checkbox(Localization::T("THIRD_PERSON_CENTERED"), &B("Misc.ThirdPersonCentered"));
                    ImGui::SameLine();
                    ImGui::Checkbox(Localization::T("ADS_FIRST_PERSON"), &B("Misc.ThirdPersonADSFirstPerson"));
                }
                else
                {
                    bool bTP = B("Player.ThirdPerson");
                    if (ImGui::Checkbox(Localization::T("THIRD_PERSON"), &bTP))
                    {
                        Cheats::ToggleThirdPerson();
                        if (B("Player.ThirdPerson"))
                        {
                            B("Player.OverShoulder") = false;
                            B("Player.Freecam") = false;
                        }
                    }

                    bool bOTS = B("Player.OverShoulder");
                    if (ImGui::Checkbox(Localization::T("OVER_SHOULDER"), &bOTS))
                    {
                        B("Player.OverShoulder") = bOTS;
                        if (bOTS)
                        {
                            B("Player.ThirdPerson") = false;
                            B("Player.Freecam") = false;
                        }
                    }

                    bool bFreecam = B("Player.Freecam");
                    if (ImGui::Checkbox(Localization::T("FREE_CAM"), &bFreecam)) Cheats::ToggleFreecam();
                }
                
                ImGui::Separator();
                ImGui::Text(Localization::T("PLAYER_PROGRESSION"));
                static int xpAmount = 1;
                ImGui::InputInt(Localization::T("EXPERIENCE_LEVEL"), &xpAmount);
                if (ImGui::Button(Localization::T("SET_EXPERIENCE_LEVEL")))
                {
                    Cheats::SetExperienceLevel(xpAmount);
                }
                
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("TAB_WORLD")))
            {
                using namespace ConfigManager;
                ImGui::Text(Localization::T("WORLD_ACTIONS"));
                if (ImGui::Button(Localization::T("KILL_ENEMIES"))) Cheats::KillEnemies();
                if (ImGui::Button(Localization::T("CLEAR_GROUND_ITEMS"))) Cheats::ClearGroundItems();
                if (ImGui::Button(Localization::T("TELEPORT_LOOT"))) Cheats::TeleportLoot();
                if (ImGui::Button(Localization::T("SPAWN_ITEMS"))) Cheats::SpawnItems();
                if (ImGui::Button(Localization::T("GIVE_5_LEVELS"))) Cheats::GiveLevels();

                ImGui::Separator();
                if (ImGui::Checkbox(Localization::T("PLAYERS_ONLY"), &B("Player.PlayersOnly"))) Cheats::TogglePlayersOnly();

                if (ImGui::SliderFloat(Localization::T("GAME_SPEED"), &F("Player.GameSpeed"), 0.1f, 10.0f))
                    Cheats::SetGameSpeed(F("Player.GameSpeed"));

                ImGui::Separator();
                ImGui::Checkbox(Localization::T("MAP_TELEPORT"), &B("Misc.MapTeleport"));
                AddDefaultTooltip("Quickly make and remove a pin on the map to teleport to that location.");
                ImGui::SliderFloat(Localization::T("MAP_TELEPORT_WINDOW"), &F("Misc.MapTPWindow"), 0.5f, 5.0f);

                ImGui::Separator();
                ImGui::Text(Localization::T("CURRENCY_SETTINGS"));
                static int CurrencyAmount = 1000;
                ImGui::InputInt("##Amount", &CurrencyAmount);
                if (ImGui::Button(Localization::T("CASH"))) Cheats::AddCurrency("Cash", CurrencyAmount);
                ImGui::SameLine();
                if (ImGui::Button(Localization::T("ERIDIUM"))) Cheats::AddCurrency("eridium", CurrencyAmount);
                ImGui::SameLine();
                if (ImGui::Button(Localization::T("VC_TICKETS"))) Cheats::AddCurrency("VaultCard01_Tokens", CurrencyAmount);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("AIMBOT")))
            {
                using namespace ConfigManager;
                if (ImGui::TreeNode(Localization::T("STANDARD_AIMBOT_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("REQUIRE_LOS"), &B("Aimbot.LOS"));
                    ImGui::Checkbox(Localization::T("DRAW_FOV"), &B("Aimbot.DrawFOV"));
                    ImGui::Checkbox(Localization::T("DRAW_ARROW"), &B("Aimbot.DrawArrow"));
                    ImGui::Checkbox(Localization::T("SMOOTH_AIM"), &B("Aimbot.Smooth"));
                    if (B("Aimbot.Smooth"))
                        ImGui::SliderFloat(Localization::T("SMOOTHING"), &F("Aimbot.SmoothingVector"), 1.0f, 20.0f);
                    ImGui::SliderFloat(Localization::T("AIMBOT_FOV"), &F("Aimbot.MaxFOV"), 1.0f, 180.0f);
                    ImGui::SliderFloat(Localization::T("MAX_DISTANCE"), &F("Aimbot.MaxDistance"), 1.0f, 500.0f);
                    
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
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("MISC_SETTINGS")))
                {
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
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("SILENT_AIM_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("REQUIRE_LOS"), &B("SilentAim.RequiresLOS"));
                    ImGui::Checkbox(Localization::T("DRAW_FOV"), &B("SilentAim.DrawFOV"));
                    
                    if (ImGui::BeginCombo(Localization::T("TARGET_BONE"), S("SilentAim.Bone").c_str()))
                    {
                        for (auto& pair : BoneOptions)
                        {
                            bool is_selected = (S("SilentAim.Bone") == pair.second);
                            if (ImGui::Selectable(pair.first, is_selected))
                                S("SilentAim.Bone") = pair.second;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(Localization::T("CHOOSE_BONE_TOOLTIP"));
                    ImGui::TreePop();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("TAB_WEAPON")))
            {
                using namespace ConfigManager;
                ImGui::Checkbox(Localization::T("INSTANT_HIT"), &B("Weapon.InstantHit"));
                AddDefaultTooltip("Increases bullet speed to effectively hit targets instantly.");
                if (B("Weapon.InstantHit"))
                {
                    ImGui::SliderFloat(Localization::T("PROJECTILE_SPEED"), &F("Weapon.ProjectileSpeedMultiplier"), 1.0f, 9999.0f);
                }
                ImGui::Separator();
                
                ImGui::Checkbox(Localization::T("RAPID_FIRE"), &B("Weapon.RapidFire"));
                if (B("Weapon.RapidFire"))
                {
                    ImGui::SliderFloat(Localization::T("FIRE_RATE_MODIFIER"), &F("Weapon.FireRate"), 0.1f, 10.0f, "%.1f");
                }
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("NO_RECOIL"), &B("Weapon.NoRecoil"));
                if (B("Weapon.NoRecoil"))
                {
                    ImGui::SliderFloat(Localization::T("RECOIL_REDUCTION"), &F("Weapon.RecoilReduction"), 0.0f, 1.0f);
                }
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("NO_SWAY"), &B("Weapon.NoSway"));
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("INSTANT_RELOAD"), &B("Weapon.InstantReload"));
                GUI::HostOnlyTooltip();
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("HOMING_PROJECTILES"), &B("Weapon.HomingProjectiles"));
                if (B("Weapon.HomingProjectiles"))
                {
                    ImGui::SliderFloat(Localization::T("HOMING_RANGE"), &F("Weapon.HomingRange"), 1.0f, 200.0f);
                }
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));
                
                if (B("Trigger.Enabled"))
                {
                    ImGui::Indent();
                    ImGui::Checkbox(Localization::T("REQUIRE_KEY_HELD"), &B("Trigger.RequireKeyHeld"));
                    ImGui::Checkbox(Localization::T("TARGET_ALL"), &B("Trigger.TargetAll"));
                    ImGui::Unindent();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("TAB_CONFIG")))
            {
                using namespace ConfigManager;
                if (ImGui::TreeNode(Localization::T("ESP_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("SHOW_BOX"), &B("ESP.ShowBox"));
                    ImGui::Checkbox(Localization::T("SHOW_DISTANCE"), &B("ESP.ShowEnemyDistance"));
                    ImGui::Checkbox(Localization::T("SHOW_BONES"), &B("ESP.Bones"));
                    ImGui::Checkbox(Localization::T("SHOW_NAME"), &B("ESP.ShowEnemyName"));
                    
                    ImGui::ColorEdit4(Localization::T("ENEMY_COLOR"), (float*)&Color("ESP.EnemyColor"), ImGuiColorEditFlags_NoInputs);
                    
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("DEBUG")))
                {
                    ImGui::Checkbox(Localization::T("ENABLE_EVENT_DEBUG_LOGS"), &B("Misc.Debug"));
                    
                    bool bRecording = Logger::IsRecording();
                    if (ImGui::Checkbox(Localization::T("ENABLE_EVENT_RECORDING"), &bRecording))
                    {
                        if (bRecording) Logger::StartRecording();
                        else Logger::StopRecording();
                    }

                    if (ImGui::Button("Dump GObjects")) Cheats::DumpObjects();
                    
                    ImGui::TreePop();
                }
                
                if (ImGui::Button(Localization::T("SAVE_SETTINGS")))
                    ConfigManager::SaveSettings();
                ImGui::SameLine();
                if (ImGui::Button(Localization::T("LOAD_SETTINGS")))
                    ConfigManager::LoadSettings();

                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                ImGui::TextWrapped(Localization::T("VOLATILE_HINT"));
                ImGui::PopStyleColor();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("TAB_HOTKEYS")))
            {
                HotkeyManager::RenderHotkeyTab();
                ImGui::EndTabItem();
            }

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}
