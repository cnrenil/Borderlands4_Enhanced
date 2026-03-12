#include "pch.h"
#include "ConfigManager.h"


extern bool SettingsLoaded;

#include "pch.h"
#include "ConfigManager.h"

extern bool SettingsLoaded;

#define SAVE_BOOL(section, key, val) WritePrivateProfileStringA(section, key, val ? "1" : "0", IniPath)
#define SAVE_FLOAT(section, key, val) WritePrivateProfileStringA(section, key, std::to_string(val).c_str(), IniPath)
#define SAVE_INT(section, key, val) WritePrivateProfileStringA(section, key, std::to_string(val).c_str(), IniPath)
#define SAVE_STR(section, key, val) WritePrivateProfileStringA(section, key, val.c_str(), IniPath)

#define LOAD_BOOL(section, key, var) var = GetPrivateProfileIntA(section, key, var, IniPath) != 0
#define LOAD_INT(section, key, var) var = GetPrivateProfileIntA(section, key, var, IniPath)
#define LOAD_FLOAT(section, key, var) { char buf[64]; GetPrivateProfileStringA(section, key, std::to_string(var).c_str(), buf, sizeof(buf), IniPath); try { var = std::stof(buf); } catch(...) {} }
#define LOAD_STR(section, key, var) { char buf[512]; GetPrivateProfileStringA(section, key, var.c_str(), buf, sizeof(buf), IniPath); var = std::string(buf); }

void ConfigManager::SaveSettings()
{
	if (!SettingsLoaded) return;

	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
	std::string IniPathStr = dir + "\\B4_Hack_Config.ini";
	LPCSTR IniPath = IniPathStr.c_str();

	// AimbotSettings
	SAVE_FLOAT("Aimbot", "MaxFOV", AimbotSettings.MaxFOV);
	SAVE_FLOAT("Aimbot", "MaxDistance", AimbotSettings.MaxDistance);
	SAVE_BOOL("Aimbot", "LOS", AimbotSettings.LOS);
	SAVE_FLOAT("Aimbot", "MinDistance", AimbotSettings.MinDistance);
	SAVE_BOOL("Aimbot", "Smooth", AimbotSettings.Smooth);
	SAVE_FLOAT("Aimbot", "SmoothingVector", AimbotSettings.SmoothingVector);
	SAVE_BOOL("Aimbot", "DrawArrow", AimbotSettings.DrawArrow);
	SAVE_BOOL("Aimbot", "DrawFOV", AimbotSettings.DrawFOV);
	SAVE_BOOL("Aimbot", "RequireKeyHeld", AimbotSettings.RequireKeyHeld);
	SAVE_INT("Aimbot", "AimbotKey", AimbotSettings.AimbotKey);
	SAVE_FLOAT("Aimbot", "FOVThickness", AimbotSettings.FOVThickness);
	SAVE_BOOL("Aimbot", "ArrowThickness", AimbotSettings.ArrowThickness);
	SAVE_BOOL("Aimbot", "TargetAll", AimbotSettings.TargetAll);
	SAVE_BOOL("Aimbot", "UseMouseInput", AimbotSettings.UseMouseInput);
	SAVE_FLOAT("Aimbot", "MouseSensitivity", AimbotSettings.MouseSensitivity);
	
	// ESPSettings
	SAVE_BOOL("ESP", "ShowTeam", ESPSettings.ShowTeam);
	SAVE_BOOL("ESP", "ShowBox", ESPSettings.ShowBox);
	SAVE_BOOL("ESP", "ShowEnemyDistance", ESPSettings.ShowEnemyDistance);
	SAVE_BOOL("ESP", "ShowEnemyName", ESPSettings.ShowEnemyName);
	SAVE_BOOL("ESP", "Bones", ESPSettings.Bones);
	SAVE_FLOAT("ESP", "BoneOpacity", ESPSettings.BoneOpacity);
	SAVE_BOOL("ESP", "LOS", ESPSettings.LOS);
	SAVE_BOOL("ESP", "BulletTracers", ESPSettings.BulletTracers);
	SAVE_BOOL("ESP", "TracerRainbow", ESPSettings.TracerRainbow);
	SAVE_FLOAT("ESP", "TracerDuration", ESPSettings.TracerDuration);

	// SilentAimSettings
	SAVE_FLOAT("SilentAim", "HitChance", SilentAimSettings.HitChance);
	SAVE_BOOL("SilentAim", "RequiresLOS", SilentAimSettings.RequiresLOS);
	SAVE_BOOL("SilentAim", "DrawFOV", SilentAimSettings.DrawFOV);
	SAVE_BOOL("SilentAim", "DrawArrow", SilentAimSettings.DrawArrow);
	SAVE_FLOAT("SilentAim", "ArrowThickness", SilentAimSettings.ArrowThickness);
	SAVE_FLOAT("SilentAim", "FOVThickness", SilentAimSettings.FOVThickness);
	SAVE_BOOL("SilentAim", "TargetAll", SilentAimSettings.TargetAll);
	SAVE_BOOL("SilentAim", "MagicBullet", SilentAimSettings.MagicBullet);

	// WeaponSettings
	SAVE_BOOL("Weapon", "InstantHitEnabled", WeaponSettings.InstantHitEnabled);
	SAVE_FLOAT("Weapon", "ProjectileSpeedMultiplier", WeaponSettings.ProjectileSpeedMultiplier);
	SAVE_BOOL("Weapon", "RapidFireEnabled", WeaponSettings.RapidFireEnabled);
	SAVE_BOOL("Weapon", "NoRecoilEnabled", WeaponSettings.NoRecoilEnabled);
	SAVE_FLOAT("Weapon", "RecoilReduction", WeaponSettings.RecoilReduction);
	SAVE_BOOL("Weapon", "NoSwayEnabled", WeaponSettings.NoSwayEnabled);
	SAVE_BOOL("Weapon", "HomingProjectiles", WeaponSettings.HomingProjectiles);
	SAVE_FLOAT("Weapon", "HomingRange", WeaponSettings.HomingRange);
	
	// TriggerBotSettings
	SAVE_BOOL("TriggerBot", "Enabled", TriggerBotSettings.Enabled);
	SAVE_BOOL("TriggerBot", "RequireKeyHeld", TriggerBotSettings.RequireKeyHeld);
	SAVE_INT("TriggerBot", "TriggerKey", TriggerBotSettings.TriggerKey);
	SAVE_BOOL("TriggerBot", "TargetAll", TriggerBotSettings.TargetAll);

	// MiscSettings
	SAVE_BOOL("Misc", "Reticle", MiscSettings.Reticle);
	SAVE_FLOAT("Misc", "ReticleSize", MiscSettings.ReticleSize);
	SAVE_BOOL("Misc", "ReticleWhenThrowing", MiscSettings.ReticleWhenThrowing);
	SAVE_BOOL("Misc", "CrossReticle", MiscSettings.CrossReticle);
	SAVE_BOOL("Misc", "EnableFOV", MiscSettings.EnableFOV);
	SAVE_FLOAT("Misc", "FOV", MiscSettings.FOV);
	SAVE_BOOL("Misc", "EnableViewModelFOV", MiscSettings.EnableViewModelFOV);
	SAVE_FLOAT("Misc", "ViewModelFOV", MiscSettings.ViewModelFOV);
	SAVE_BOOL("Misc", "DisableVolumetricClouds", MiscSettings.DisableVolumetricClouds);
	SAVE_BOOL("Misc", "MapTeleport", MiscSettings.MapTeleport);
	SAVE_FLOAT("Misc", "MapTPWindow", MiscSettings.MapTPWindow);
	SAVE_BOOL("Misc", "ThirdPersonCentered", MiscSettings.ThirdPersonCentered);
	SAVE_BOOL("Misc", "ThirdPersonOTS", MiscSettings.ThirdPersonOTS);
	SAVE_BOOL("Misc", "ShouldAutoSave", MiscSettings.ShouldAutoSave);
	SAVE_BOOL("Misc", "ShouldSaveCVars", MiscSettings.ShouldSaveCVars);
	SAVE_INT("Misc", "CurrentLanguage", static_cast<int>(MiscSettings.CurrentLanguage));

	// TextVars
	SAVE_STR("Text", "AimbotBone", TextVars.AimbotBone);
	SAVE_STR("Text", "SilentAimBone", TextVars.SilentAimBone);
	SAVE_STR("Text", "DebugFunctionNameMustInclude", TextVars.DebugFunctionNameMustInclude);
	SAVE_STR("Text", "DebugFunctionObjectMustInclude", TextVars.DebugFunctionObjectMustInclude);

	// CVars
	// CVars
	if (MiscSettings.ShouldSaveCVars) {
		SAVE_BOOL("CVars", "Debug", CVars.Debug);
		SAVE_BOOL("CVars", "SecretFeatures", CVars.SecretFeatures);
		
		// The following are "Action Toggles" that call a function once and shouldn't be persistent across injection
		/*
		SAVE_BOOL("CVars", "GodMode", CVars.GodMode);
		SAVE_BOOL("CVars", "InfAmmo", CVars.InfAmmo);
		SAVE_BOOL("CVars", "Demigod", CVars.Demigod);
		SAVE_BOOL("CVars", "NoTarget", CVars.NoTarget);
		SAVE_BOOL("CVars", "PlayersOnly", CVars.PlayersOnly);
		*/

		SAVE_BOOL("CVars", "Aimbot", CVars.Aimbot);
		SAVE_BOOL("CVars", "ESP", CVars.ESP);
		SAVE_FLOAT("CVars", "Speed", CVars.Speed);
		SAVE_BOOL("CVars", "SpeedEnabled", CVars.SpeedEnabled);
		SAVE_BOOL("CVars", "SilentAim", CVars.SilentAim);
		SAVE_BOOL("CVars", "Reticle", CVars.Reticle);
		SAVE_BOOL("CVars", "TriggerBot", CVars.TriggerBot);
		SAVE_BOOL("CVars", "RenderOptions", CVars.RenderOptions);
		SAVE_FLOAT("CVars", "FOV", CVars.FOV);
		SAVE_BOOL("CVars", "ListPlayers", CVars.ListPlayers);
		SAVE_BOOL("CVars", "ShootFromReticle", CVars.ShootFromReticle);
		SAVE_BOOL("CVars", "SaveDebugToFile", CVars.SaveDebugToFile);
		SAVE_BOOL("CVars", "BulletTime", CVars.BulletTime);
		SAVE_FLOAT("CVars", "GameSpeed", CVars.GameSpeed);
		SAVE_BOOL("CVars", "ThirdPerson", CVars.ThirdPerson);
	}
}

void ConfigManager::LoadSettings()
{
	if (!Settings.ShouldLoad)
		return;

	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
	std::string IniPathStr = dir + "\\B4_Hack_Config.ini";
	LPCSTR IniPath = IniPathStr.c_str();

	// Check if file exists, if not, we skip loading to keep defaults
	FILE* f;
	if (fopen_s(&f, IniPath, "r") == 0) {
		fclose(f);
	} else {
		SettingsLoaded = true;
		return;
	}

	// AimbotSettings
	LOAD_FLOAT("Aimbot", "MaxFOV", AimbotSettings.MaxFOV);
	LOAD_FLOAT("Aimbot", "MaxDistance", AimbotSettings.MaxDistance);
	LOAD_BOOL("Aimbot", "LOS", AimbotSettings.LOS);
	LOAD_FLOAT("Aimbot", "MinDistance", AimbotSettings.MinDistance);
	LOAD_BOOL("Aimbot", "Smooth", AimbotSettings.Smooth);
	LOAD_FLOAT("Aimbot", "SmoothingVector", AimbotSettings.SmoothingVector);
	LOAD_BOOL("Aimbot", "DrawArrow", AimbotSettings.DrawArrow);
	LOAD_BOOL("Aimbot", "DrawFOV", AimbotSettings.DrawFOV);
	LOAD_BOOL("Aimbot", "RequireKeyHeld", AimbotSettings.RequireKeyHeld);
	int ak = AimbotSettings.AimbotKey; LOAD_INT("Aimbot", "AimbotKey", ak); AimbotSettings.AimbotKey = (ImGuiKey)ak;
	LOAD_FLOAT("Aimbot", "FOVThickness", AimbotSettings.FOVThickness);
	LOAD_BOOL("Aimbot", "ArrowThickness", AimbotSettings.ArrowThickness);
	LOAD_BOOL("Aimbot", "TargetAll", AimbotSettings.TargetAll);
	LOAD_BOOL("Aimbot", "UseMouseInput", AimbotSettings.UseMouseInput);
	LOAD_FLOAT("Aimbot", "MouseSensitivity", AimbotSettings.MouseSensitivity);

	// ESPSettings
	LOAD_BOOL("ESP", "ShowTeam", ESPSettings.ShowTeam);
	LOAD_BOOL("ESP", "ShowBox", ESPSettings.ShowBox);
	LOAD_BOOL("ESP", "ShowEnemyDistance", ESPSettings.ShowEnemyDistance);
	LOAD_BOOL("ESP", "ShowEnemyName", ESPSettings.ShowEnemyName);
	LOAD_BOOL("ESP", "Bones", ESPSettings.Bones);
	LOAD_FLOAT("ESP", "BoneOpacity", ESPSettings.BoneOpacity);
	LOAD_BOOL("ESP", "LOS", ESPSettings.LOS);
	LOAD_BOOL("ESP", "BulletTracers", ESPSettings.BulletTracers);
	LOAD_BOOL("ESP", "TracerRainbow", ESPSettings.TracerRainbow);
	LOAD_FLOAT("ESP", "TracerDuration", ESPSettings.TracerDuration);

	// SilentAimSettings
	LOAD_FLOAT("SilentAim", "HitChance", SilentAimSettings.HitChance);
	LOAD_BOOL("SilentAim", "RequiresLOS", SilentAimSettings.RequiresLOS);
	LOAD_BOOL("SilentAim", "DrawFOV", SilentAimSettings.DrawFOV);
	LOAD_BOOL("SilentAim", "DrawArrow", SilentAimSettings.DrawArrow);
	LOAD_FLOAT("SilentAim", "ArrowThickness", SilentAimSettings.ArrowThickness);
	LOAD_FLOAT("SilentAim", "FOVThickness", SilentAimSettings.FOVThickness);
	LOAD_BOOL("SilentAim", "TargetAll", SilentAimSettings.TargetAll);
	LOAD_BOOL("SilentAim", "MagicBullet", SilentAimSettings.MagicBullet);

	// WeaponSettings
	LOAD_BOOL("Weapon", "InstantHitEnabled", WeaponSettings.InstantHitEnabled);
	LOAD_FLOAT("Weapon", "ProjectileSpeedMultiplier", WeaponSettings.ProjectileSpeedMultiplier);
	LOAD_BOOL("Weapon", "RapidFireEnabled", WeaponSettings.RapidFireEnabled);
	LOAD_BOOL("Weapon", "NoRecoilEnabled", WeaponSettings.NoRecoilEnabled);
	LOAD_FLOAT("Weapon", "RecoilReduction", WeaponSettings.RecoilReduction);
	LOAD_BOOL("Weapon", "NoSwayEnabled", WeaponSettings.NoSwayEnabled);
	LOAD_BOOL("Weapon", "HomingProjectiles", WeaponSettings.HomingProjectiles);
	LOAD_FLOAT("Weapon", "HomingRange", WeaponSettings.HomingRange);

	// TriggerBotSettings
	LOAD_BOOL("TriggerBot", "Enabled", TriggerBotSettings.Enabled);
	LOAD_BOOL("TriggerBot", "RequireKeyHeld", TriggerBotSettings.RequireKeyHeld);
	int tk = TriggerBotSettings.TriggerKey; LOAD_INT("TriggerBot", "TriggerKey", tk); TriggerBotSettings.TriggerKey = (ImGuiKey)tk;
	LOAD_BOOL("TriggerBot", "TargetAll", TriggerBotSettings.TargetAll);

	// MiscSettings
	LOAD_BOOL("Misc", "Reticle", MiscSettings.Reticle);
	LOAD_FLOAT("Misc", "ReticleSize", MiscSettings.ReticleSize);
	LOAD_BOOL("Misc", "ReticleWhenThrowing", MiscSettings.ReticleWhenThrowing);
	LOAD_BOOL("Misc", "CrossReticle", MiscSettings.CrossReticle);
	LOAD_BOOL("Misc", "EnableFOV", MiscSettings.EnableFOV);
	LOAD_FLOAT("Misc", "FOV", MiscSettings.FOV);
	LOAD_BOOL("Misc", "EnableViewModelFOV", MiscSettings.EnableViewModelFOV);
	LOAD_FLOAT("Misc", "ViewModelFOV", MiscSettings.ViewModelFOV);
	LOAD_BOOL("Misc", "DisableVolumetricClouds", MiscSettings.DisableVolumetricClouds);
	LOAD_BOOL("Misc", "MapTeleport", MiscSettings.MapTeleport);
	LOAD_FLOAT("Misc", "MapTPWindow", MiscSettings.MapTPWindow);
	LOAD_BOOL("Misc", "ThirdPersonCentered", MiscSettings.ThirdPersonCentered);
	LOAD_BOOL("Misc", "ThirdPersonOTS", MiscSettings.ThirdPersonOTS);
	LOAD_BOOL("Misc", "ShouldAutoSave", MiscSettings.ShouldAutoSave);
	LOAD_BOOL("Misc", "ShouldSaveCVars", MiscSettings.ShouldSaveCVars);
	int cl = static_cast<int>(MiscSettings.CurrentLanguage); LOAD_INT("Misc", "CurrentLanguage", cl); MiscSettings.CurrentLanguage = static_cast<Language>(cl);
	Localization::CurrentLanguage = MiscSettings.CurrentLanguage;

	// TextVars
	LOAD_STR("Text", "AimbotBone", TextVars.AimbotBone);
	LOAD_STR("Text", "SilentAimBone", TextVars.SilentAimBone);
	LOAD_STR("Text", "DebugFunctionNameMustInclude", TextVars.DebugFunctionNameMustInclude);
	LOAD_STR("Text", "DebugFunctionObjectMustInclude", TextVars.DebugFunctionObjectMustInclude);

	// CVars
	// CVars
	if (MiscSettings.ShouldSaveCVars) {
		LOAD_BOOL("CVars", "Debug", CVars.Debug);
		LOAD_BOOL("CVars", "SecretFeatures", CVars.SecretFeatures);

		/* Action Toggles Excluded
		LOAD_BOOL("CVars", "GodMode", CVars.GodMode);
		LOAD_BOOL("CVars", "InfAmmo", CVars.InfAmmo);
		LOAD_BOOL("CVars", "Demigod", CVars.Demigod);
		LOAD_BOOL("CVars", "NoTarget", CVars.NoTarget);
		LOAD_BOOL("CVars", "PlayersOnly", CVars.PlayersOnly);
		*/

		LOAD_BOOL("CVars", "Aimbot", CVars.Aimbot);
		LOAD_BOOL("CVars", "ESP", CVars.ESP);
		LOAD_FLOAT("CVars", "Speed", CVars.Speed);
		LOAD_BOOL("CVars", "SpeedEnabled", CVars.SpeedEnabled);
		LOAD_BOOL("CVars", "SilentAim", CVars.SilentAim);
		LOAD_BOOL("CVars", "Reticle", CVars.Reticle);
		LOAD_BOOL("CVars", "TriggerBot", CVars.TriggerBot);
		LOAD_BOOL("CVars", "RenderOptions", CVars.RenderOptions);
		LOAD_FLOAT("CVars", "FOV", CVars.FOV);
		LOAD_BOOL("CVars", "ListPlayers", CVars.ListPlayers);
		LOAD_BOOL("CVars", "ShootFromReticle", CVars.ShootFromReticle);
		LOAD_BOOL("CVars", "SaveDebugToFile", CVars.SaveDebugToFile);
		LOAD_BOOL("CVars", "BulletTime", CVars.BulletTime);
		LOAD_FLOAT("CVars", "GameSpeed", CVars.GameSpeed);
		LOAD_BOOL("CVars", "ThirdPerson", CVars.ThirdPerson);
	}

	SettingsLoaded = true;
	std::cout << "[Settings] Configuration loaded successfully from INI.\n";
}
