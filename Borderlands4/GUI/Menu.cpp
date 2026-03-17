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

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x > 0.0f && displaySize.y > 0.0f)
    {
        ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    }
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
                ImGui::Checkbox(Localization::T("INF_AMMO"), &B("Player.InfAmmo"));
                ImGui::Checkbox(Localization::T("INF_GRENADES"), &B("Player.InfGrenades"));
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
                ImGui::Checkbox(Localization::T("INF_VEHICLE_BOOST"), &B("Player.InfVehicleBoost"));
                ImGui::Checkbox(Localization::T("VEHICLE_SPEED_HACK"), &B("Player.VehicleSpeedEnabled"));
                if (B("Player.VehicleSpeedEnabled"))
                {
                    ImGui::SliderFloat(Localization::T("VEHICLE_SPEED_VALUE"), &F("Player.VehicleSpeed"), 1.0f, 20.0f, "%.1f");
                }
                ImGui::Checkbox(Localization::T("INF_GLIDE_STAMINA"), &B("Player.InfGlideStamina"));
                
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
                    ImGui::Checkbox(Localization::T("OTS_ADS_CAMERA_OVERRIDE"), &B("Misc.OTSADSOverride"));
                    if (B("Misc.OTSADSOverride"))
                    {
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_X"), &F("Misc.OTSADS_X"), -500.0f, 500.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_Y"), &F("Misc.OTSADS_Y"), -200.0f, 200.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_OFFSET_Z"), &F("Misc.OTSADS_Z"), -200.0f, 200.0f);
                        ImGui::SliderFloat(Localization::T("OTS_ADS_FOV"), &F("Misc.OTSADSFOV"), 20.0f, 180.0f, "%.1f");
                        ImGui::SliderFloat(Localization::T("OTS_ADS_BLEND_TIME"), &F("Misc.OTSADSBlendTime"), 0.01f, 1.00f, "%.2fs");
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
                    ImGui::Checkbox(Localization::T("TRIGGERBOT"), &B("Trigger.Enabled"));
                    ImGui::Checkbox(Localization::T("DRAW_FOV"), &B("Aimbot.DrawFOV"));
                    if (B("Aimbot.DrawFOV"))
                        ImGui::SliderFloat(Localization::T("FOV_LINE_THICKNESS"), &F("Aimbot.FOVThickness"), 1.0f, 6.0f, "%.1f");
                    ImGui::Checkbox(Localization::T("DRAW_ARROW"), &B("Aimbot.DrawArrow"));
                    if (B("Aimbot.DrawArrow"))
                        ImGui::SliderFloat(Localization::T("ARROW_LINE_THICKNESS"), &F("Aimbot.ArrowThickness"), 1.0f, 6.0f, "%.1f");
                    ImGui::Checkbox(Localization::T("SMOOTH_AIM"), &B("Aimbot.Smooth"));
                    if (B("Aimbot.Smooth"))
                        ImGui::SliderFloat(Localization::T("SMOOTHING"), &F("Aimbot.SmoothingVector"), 1.0f, 20.0f);
                    ImGui::SeparatorText(Localization::T("TARGET_MODE"));
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

                    if (B("Trigger.Enabled"))
                    {
                        ImGui::Indent();
                        ImGui::Checkbox(Localization::T("REQUIRE_KEY_HELD"), &B("Trigger.RequireKeyHeld"));
                        ImGui::Checkbox(Localization::T("TARGET_ALL"), &B("Trigger.TargetAll"));
                        ImGui::Unindent();
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode(Localization::T("MISC_SETTINGS")))
                {
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
                    ImGui::Separator();
                    ImGui::Checkbox(Localization::T("ENABLE_RETICLE"), &B("Misc.Reticle"));
                    if (B("Misc.Reticle"))
                    {
                        ImGui::Checkbox(Localization::T("RETICLE_CROSSHAIR"), &B("Misc.CrossReticle"));
                        ImGui::SliderFloat(Localization::T("RETICLE_SIZE"), &F("Misc.ReticleSize"), 2.0f, 30.0f, "%.1f");
                        ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_X"), &Vec2("Misc.ReticlePosition").x, -200.0f, 200.0f, "%.0f");
                        ImGui::SliderFloat(Localization::T("RETICLE_OFFSET_Y"), &Vec2("Misc.ReticlePosition").y, -200.0f, 200.0f, "%.0f");
                        ImGui::ColorEdit4(Localization::T("RETICLE_COLOR"), (float*)&Color("Misc.ReticleColor"), ImGuiColorEditFlags_NoInputs);
                    }
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
                ImGui::Checkbox(Localization::T("NO_SPREAD"), &B("Weapon.NoSpread"));
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("NO_SWAY"), &B("Weapon.NoSway"));
                
                ImGui::Separator();
                ImGui::Checkbox(Localization::T("INSTANT_RELOAD"), &B("Weapon.InstantReload"));
                GUI::HostOnlyTooltip();
                ImGui::Checkbox(Localization::T("INSTANT_SWAP"), &B("Weapon.InstantSwap"));
                GUI::HostOnlyTooltip();
                ImGui::Checkbox(Localization::T("NO_AMMO_CONSUME"), &B("Weapon.NoAmmoConsume"));
                GUI::HostOnlyTooltip();
                
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localization::T("TAB_CONFIG")))
            {
                using namespace ConfigManager;
                if (ImGui::TreeNode(Localization::T("ESP_SETTINGS")))
                {
                    ImGui::Checkbox(Localization::T("SHOW_TEAM"), &B("ESP.ShowTeam"));
                    ImGui::Checkbox(Localization::T("SHOW_BOX"), &B("ESP.ShowBox"));
                    ImGui::Checkbox(Localization::T("SHOW_DISTANCE"), &B("ESP.ShowEnemyDistance"));
                    ImGui::Checkbox(Localization::T("SHOW_BONES"), &B("ESP.Bones"));
                    ImGui::Checkbox(Localization::T("SHOW_NAME"), &B("ESP.ShowEnemyName"));
                    ImGui::Checkbox(Localization::T("SHOW_ENEMY_INDICATOR"), &B("ESP.ShowEnemyIndicator"));
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
                    ImGui::Checkbox(Localization::T("SHOW_LOOT_NAME"), &B("ESP.ShowLootName"));
                    if (B("ESP.ShowLootName"))
                    {
                        ImGui::SliderFloat(Localization::T("LOOT_MAX_DISTANCE"), &F("ESP.LootMaxDistance"), 10.0f, 1000.0f, "%.0f");
                        ImGui::ColorEdit4(Localization::T("LOOT_COLOR"), (float*)&Color("ESP.LootColor"), ImGuiColorEditFlags_NoInputs);
                    }
                    
                    ImGui::ColorEdit4(Localization::T("ENEMY_COLOR"), (float*)&Color("ESP.EnemyColor"), ImGuiColorEditFlags_NoInputs);
                    if (B("ESP.ShowTeam"))
                    {
                        ImGui::ColorEdit4(Localization::T("TEAM_COLOR"), (float*)&Color("ESP.TeamColor"), ImGuiColorEditFlags_NoInputs);
                    }
                    
                    ImGui::TreePop();
                }

#if BL4_DEBUG_BUILD
                if (ImGui::TreeNode(Localization::T("DEBUG")))
                {
                    ImGui::Checkbox(Localization::T("ENABLE_EVENT_DEBUG_LOGS"), &B("Misc.Debug"));
                    ImGui::Checkbox(Localization::T("ENABLE_PING_DUMP"), &B("Misc.PingDump"));
                    ImGui::Checkbox(Localization::T("ENABLE_PP_TRACE"), &B("Misc.PostProcessTrace"));

                    bool bRecording = Logger::IsRecording();
                    if (ImGui::Checkbox(Localization::T("ENABLE_EVENT_RECORDING"), &bRecording))
                    {
                        if (bRecording) Logger::StartRecording();
                        else Logger::StopRecording();
                    }

                    if (ImGui::Button(Localization::T("DUMP_GOBJECTS"))) Cheats::DumpObjects();
                    if (ImGui::Button(Localization::T("KILL_SYMBIOTE"))) { AntiDebug::Bypass(); }
                    GUI::AddDefaultTooltip(Localization::T("KILL_SYMBIOTE_TOOLTIP"));

                    ImGui::TreePop();
                }
#endif
                
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
