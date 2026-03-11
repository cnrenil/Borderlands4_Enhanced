#include "pch.h"
#include "Menu.h"
#include "Config/ConfigManager.h"
#include "Cheats.h"

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
	
	if (ImGui::Begin("Borderlands 4 Hack", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::SeparatorText("Hello, Have Fun Cheating!");

		if (ImGui::BeginTabBar("MainTabBar"))
		{
			if (ImGui::BeginTabItem(Localization::T("TAB_ABOUT")))
			{
				static const char* HostLangs[] = { "English", "简体中文" };
				int CurrentLangIdx = (int)MiscSettings.CurrentLanguage;
				if (ImGui::Combo(Localization::T("LANGUAGE"), &CurrentLangIdx, HostLangs, IM_ARRAYSIZE(HostLangs)))
				{
					MiscSettings.CurrentLanguage = (Language)CurrentLangIdx;
					Localization::CurrentLanguage = MiscSettings.CurrentLanguage;
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
                ImGui::Checkbox(Localization::T("ESP"), &CVars.ESP);
                ImGui::Checkbox(Localization::T("AIMBOT"), &CVars.Aimbot);
                ImGui::Checkbox(Localization::T("SILENT_AIM"), &CVars.SilentAim);
                ImGui::Checkbox(Localization::T("GODMODE"), &CVars.GodMode);
                if (ImGui::Checkbox(Localization::T("DEMIGOD"), &CVars.Demigod)) Cheats::ToggleDemigod();
                if (ImGui::Checkbox(Localization::T("NO_TARGET"), &CVars.NoTarget)) Cheats::ToggleNoTarget();
                
                bool bTP = CVars.ThirdPerson;
                if (ImGui::Checkbox(Localization::T("THIRD_PERSON"), &bTP)) Cheats::ToggleThirdPerson();
                if (CVars.ThirdPerson)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox(Localization::T("THIRD_PERSON_CENTERED"), &MiscSettings.ThirdPersonCentered);
                    ImGui::SameLine();
                    ImGui::Checkbox(Localization::T("THIRD_PERSON_OTS"), &MiscSettings.ThirdPersonOTS);
                    
                    if (MiscSettings.ThirdPersonOTS)
                    {
                        ImGui::Indent();
                        ImGui::SliderFloat(Localization::T("OTS_OFFSET_X"), &MiscSettings.OTS_X, -500.0f, 500.0f);
                        ImGui::SliderFloat(Localization::T("OTS_OFFSET_Y"), &MiscSettings.OTS_Y, -200.0f, 200.0f);
                        ImGui::SliderFloat(Localization::T("OTS_OFFSET_Z"), &MiscSettings.OTS_Z, -200.0f, 200.0f);
                        ImGui::Unindent();
                    }
                    else
                    {
                        ImGui::SameLine();
                        ImGui::Checkbox(Localization::T("ADS_FIRST_PERSON"), &MiscSettings.ThirdPersonADSFirstPerson);
                    }
                }

                if (!CVars.ThirdPerson)
                {
                    bool bFreecam = CVars.Freecam;
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
                ImGui::Text(Localization::T("WORLD_ACTIONS"));
                if (ImGui::Button(Localization::T("KILL_ENEMIES"))) Cheats::KillEnemies();
                if (ImGui::Button(Localization::T("CLEAR_GROUND_ITEMS"))) Cheats::ClearGroundItems();
                if (ImGui::Button(Localization::T("TELEPORT_LOOT"))) Cheats::TeleportLoot();
                if (ImGui::Button(Localization::T("SPAWN_ITEMS"))) Cheats::SpawnItems();
                if (ImGui::Button(Localization::T("GIVE_5_LEVELS"))) Cheats::GiveLevels();

                ImGui::Separator();
                if (ImGui::Checkbox(Localization::T("PLAYERS_ONLY"), &CVars.PlayersOnly)) Cheats::TogglePlayersOnly();

                if (ImGui::SliderFloat(Localization::T("GAME_SPEED"), &CVars.GameSpeed, 0.1f, 10.0f))
                    Cheats::SetGameSpeed(CVars.GameSpeed);

                ImGui::Separator();
                ImGui::Checkbox(Localization::T("MAP_TELEPORT"), &MiscSettings.MapTeleport);
                ImGui::SliderFloat(Localization::T("MAP_TELEPORT_WINDOW"), &MiscSettings.MapTPWindow, 0.5f, 5.0f);

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
                if (ImGui::TreeNode(Localization::T("STANDARD_AIMBOT_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("REQUIRE_LOS"), &AimbotSettings.LOS);
                    ImGui::Checkbox(Localization::T("DRAW_FOV"), &AimbotSettings.DrawFOV);
                    ImGui::Checkbox(Localization::T("DRAW_ARROW"), &AimbotSettings.DrawArrow);
                    ImGui::Checkbox(Localization::T("SMOOTH_AIM"), &AimbotSettings.Smooth);
                    if (AimbotSettings.Smooth)
                        ImGui::SliderFloat(Localization::T("SMOOTHING"), &AimbotSettings.SmoothingVector, 1.0f, 20.0f);
                    ImGui::SliderFloat(Localization::T("AIMBOT_FOV"), &AimbotSettings.MaxFOV, 1.0f, 180.0f);
                    ImGui::SliderFloat(Localization::T("MAX_DISTANCE"), &AimbotSettings.MaxDistance, 1.0f, 500.0f);
                    
                    if (ImGui::BeginCombo(Localization::T("TARGET_BONE"), TextVars.AimbotBone.c_str()))
                    {
                        for (auto& pair : BoneOptions)
                        {
                            bool is_selected = (TextVars.AimbotBone == pair.second);
                            if (ImGui::Selectable(pair.first, is_selected))
                                TextVars.AimbotBone = pair.second;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("MISC_SETTINGS")))
                {
                    if (ImGui::Checkbox(Localization::T("ENABLE_FOV_CHANGER"), &MiscSettings.EnableFOV))
                    {
                        if (MiscSettings.EnableFOV && GVars.POV)
                        {
                            MiscSettings.FOV = GVars.POV->fov;
                        }
                    }
                    if (MiscSettings.EnableFOV)
                    {
                        ImGui::SliderFloat(Localization::T("FOV_VALUE"), &MiscSettings.FOV, 60.0f, 180.0f);
                    }
                    ImGui::Checkbox(Localization::T("ENABLE_VIEWMODEL_FOV"), &MiscSettings.EnableViewModelFOV);
                    if (MiscSettings.EnableViewModelFOV)
                    {
                        ImGui::SliderFloat(Localization::T("VIEWMODEL_FOV_VALUE"), &MiscSettings.ViewModelFOV, 60.0f, 150.0f);
                    }
                    ImGui::Checkbox(Localization::T("DISABLE_VOLUMETRIC_CLOUDS"), &MiscSettings.DisableVolumetricClouds);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("SILENT_AIM_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("REQUIRE_LOS"), &SilentAimSettings.RequiresLOS);
                    ImGui::Checkbox(Localization::T("DRAW_FOV"), &SilentAimSettings.DrawFOV);
                    
                    if (ImGui::BeginCombo(Localization::T("TARGET_BONE"), TextVars.SilentAimBone.c_str()))
                    {
                        for (auto& pair : BoneOptions)
                        {
                            bool is_selected = (TextVars.SilentAimBone == pair.second);
                            if (ImGui::Selectable(pair.first, is_selected))
                                TextVars.SilentAimBone = pair.second;
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
                ImGui::Checkbox(Localization::T("INSTANT_HIT"), &WeaponSettings.InstantHitEnabled);
                if (WeaponSettings.InstantHitEnabled)
                {
                    ImGui::SliderFloat(Localization::T("PROJECTILE_SPEED"), &WeaponSettings.ProjectileSpeedMultiplier, 1.0f, 9999.0f);
                }
                ImGui::Separator();
                
                ImGui::Checkbox(Localization::T("RAPID_FIRE"), &WeaponSettings.RapidFireEnabled);
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("NO_RECOIL"), &WeaponSettings.NoRecoilEnabled);
                if (WeaponSettings.NoRecoilEnabled)
                {
                    ImGui::SliderFloat(Localization::T("RECOIL_REDUCTION"), &WeaponSettings.RecoilReduction, 0.0f, 1.0f);
                }
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("NO_SWAY"), &WeaponSettings.NoSwayEnabled);
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("HOMING_PROJECTILES"), &WeaponSettings.HomingProjectiles);
                if (WeaponSettings.HomingProjectiles)
                {
                    ImGui::SliderFloat(Localization::T("HOMING_RANGE"), &WeaponSettings.HomingRange, 1.0f, 200.0f);
                }
                
                ImGui::Separator();
                if (ImGui::Checkbox(Localization::T("TRIGGERBOT"), &CVars.TriggerBot))
                {
                    TriggerBotSettings.Enabled = CVars.TriggerBot;
                }
                
                if (CVars.TriggerBot)
                {
                    ImGui::Indent();
                    ImGui::Checkbox(Localization::T("REQUIRE_KEY_HELD"), &TriggerBotSettings.RequireKeyHeld);
                    ImGui::Checkbox(Localization::T("TARGET_ALL"), &TriggerBotSettings.TargetAll);
                    ImGui::Unindent();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("CONFIG")))
            {
                if (ImGui::TreeNode(Localization::T("ESP_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("SHOW_BOX"), &ESPSettings.ShowBox);
                    ImGui::Checkbox(Localization::T("SHOW_DISTANCE"), &ESPSettings.ShowEnemyDistance);
                    ImGui::Checkbox(Localization::T("SHOW_BONES"), &ESPSettings.Bones);
                    ImGui::Checkbox(Localization::T("SHOW_NAME"), &ESPSettings.ShowEnemyName);
                    
                    ImGui::ColorEdit4(Localization::T("ENEMY_COLOR"), (float*)&ESPSettings.EnemyColor, ImGuiColorEditFlags_NoInputs);
                    
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("DEBUG")))
                {
                    ImGui::Checkbox(Localization::T("ENABLE_EVENT_DEBUG_LOGS"), &CVars.Debug);
                    ImGui::TreePop();
                }
                
                if (ImGui::Button(Localization::T("SAVE_SETTINGS")))
                    ConfigManager::SaveSettings();
                ImGui::SameLine();
                if (ImGui::Button(Localization::T("LOAD_SETTINGS")))
                    ConfigManager::LoadSettings();

                ImGui::EndTabItem();
            }

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}
