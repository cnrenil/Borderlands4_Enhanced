#include "pch.h"


namespace ConfigManager
{
    std::unordered_map<std::string, ConfigValue> ConfigMap;

    void Register(const std::string& key, ConfigValue defaultValue) {
        if (ConfigMap.find(key) == ConfigMap.end()) {
            ConfigMap[key] = defaultValue;
        }
    }

    bool Exists(const std::string& key) {
        return ConfigMap.find(key) != ConfigMap.end();
    }

    void Initialize()
    {
        // ESP
        Register("ESP.ShowTeam", true);
        Register("ESP.ShowBox", true);
        Register("ESP.ShowEnemyDistance", true);
        Register("ESP.ShowEnemyName", true);
        Register("ESP.Bones", true);
        Register("ESP.BoneOpacity", 1.0f);
        Register("ESP.EnemyColor", ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        Register("ESP.TeamColor", ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        Register("ESP.TargetColor", ImVec4(0.7f, 0.0f, 1.0f, 1.0f));
        Register("ESP.LOS", false);
        Register("ESP.BulletTracers", true);
        Register("ESP.TracerRainbow", true);
        Register("ESP.TracerDuration", 2.0f);
        Register("ESP.TracerColor", ImVec4(1.0f, 0.5f, 0.0f, 1.0f));

        // Aimbot
        Register("Aimbot.Enabled", false);
        Register("Aimbot.MaxFOV", 15.0f);
        Register("Aimbot.MaxDistance", 100.0f);
        Register("Aimbot.LOS", true);
        Register("Aimbot.MinDistance", 2.0f);
        Register("Aimbot.Smooth", false);
        Register("Aimbot.SmoothingVector", 5.0f);
        Register("Aimbot.DrawArrow", false);
        Register("Aimbot.DrawFOV", false);
        Register("Aimbot.RequireKeyHeld", true);
        Register("Aimbot.FOVThickness", 1.0f);
        Register("Aimbot.ArrowThickness", 2.0f);
        Register("Aimbot.TargetAll", false);
        Register("Aimbot.UseMouseInput", true);
        Register("Aimbot.MouseSensitivity", 1.0f);
        Register("Aimbot.Bone", std::string("Head"));

        // TriggerBot
        Register("Trigger.Enabled", false);
        Register("Trigger.RequireKeyHeld", true);
        Register("Trigger.TargetAll", false);

        // Weapon
        Register("Weapon.InstantHit", false);
        Register("Weapon.ProjectileSpeedMultiplier", 999.0f);
        Register("Weapon.RapidFire", false);
        Register("Weapon.NoRecoil", false);
        Register("Weapon.NoSpread", false);
        Register("Weapon.RecoilReduction", 1.0f);
        Register("Weapon.NoSway", false);
        Register("Weapon.HomingProjectiles", false);
        Register("Weapon.HomingRange", 50.0f);
        Register("Weapon.FireRate", 1.0f);
        Register("Weapon.InstantReload", false);

        // Player / CVars
        Register("Player.GodMode", false);
        Register("Player.Demigod", false);
        Register("Player.InfAmmo", false);
        Register("Player.NoTarget", false);
        Register("Player.PlayersOnly", false);
        Register("Player.GameSpeed", 1.0f);
        Register("Player.SpeedEnabled", false);
        Register("Player.Speed", 1.0f);
        Register("Player.FOV", 120.0f);
        Register("Player.ThirdPerson", false);
        Register("Player.OverShoulder", false);
        Register("Player.Freecam", false);
        Register("Player.Flight", false);
        Register("Player.FlightSpeed", 1.0f);
        Register("Player.ESP", true);

        // SilentAim
        Register("SilentAim.Enabled", false);
        Register("SilentAim.HitChance", 100.0f);
        Register("SilentAim.RequiresLOS", false);
        Register("SilentAim.DrawFOV", false);
        Register("SilentAim.DrawArrow", false);
        Register("SilentAim.ArrowThickness", 2.0f);
        Register("SilentAim.FOVThickness", 1.0f);
        Register("SilentAim.TargetAll", false);
        Register("SilentAim.MagicBullet", false);
        Register("SilentAim.Bone", std::string("Head"));

        // Misc
        Register("Misc.Reticle", false);
        Register("Misc.ReticleColor", ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        Register("Misc.ReticleSize", 5.0f);
        Register("Misc.ReticlePosition", ImVec2(0.0f, 0.0f));
        Register("Misc.ReticleWhenThrowing", false);
        Register("Misc.CrossReticle", true);
        Register("Misc.EnableFOV", false);
        Register("Misc.FOV", 100.0f);
        Register("Misc.ADSFOVScale", 0.7f);
        Register("Misc.EnableViewModelFOV", false);
        Register("Misc.ViewModelFOV", 90.0f);
        Register("Misc.DisableVolumetricClouds", false);
        Register("Misc.ShouldAutoSave", true);
        Register("Misc.ShouldSaveCVars", true);
        Register("Misc.MapTeleport", false);
        Register("Misc.MapTPWindow", 2.0f);
        Register("Misc.ThirdPersonCentered", false);
        Register("Misc.ThirdPersonOTS", true);
        Register("Misc.ThirdPersonADSFirstPerson", true);
        Register("Misc.OTS_X", -150.0f);
        Register("Misc.OTS_Y", 60.0f);
        Register("Misc.OTS_Z", 20.0f);
        Register("Misc.OTSADSFOVBoost", false);
        Register("Misc.OTSADSFOVScale", 1.2f);
        Register("Misc.FreecamBlockInput", true);
        Register("Misc.Language", 0);
        Register("Misc.Debug", false);
        Register("Misc.RenderOptions", false);
    }

    static std::string GetIniPath() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
        return dir + "\\B4_Hack_Config.ini";
    }

    void SaveSettings()
    {
        Initialize();
        std::string path = GetIniPath();

        for (auto const& [key, val] : ConfigMap) {
            size_t dot = key.find('.');
            std::string section = (dot != std::string::npos) ? key.substr(0, dot) : "General";
            std::string name = (dot != std::string::npos) ? key.substr(dot + 1) : key;

            if (std::holds_alternative<bool>(val)) {
                WritePrivateProfileStringA(section.c_str(), name.c_str(), std::get<bool>(val) ? "1" : "0", path.c_str());
            } else if (std::holds_alternative<int>(val)) {
                WritePrivateProfileStringA(section.c_str(), name.c_str(), std::to_string(std::get<int>(val)).c_str(), path.c_str());
            } else if (std::holds_alternative<float>(val)) {
                WritePrivateProfileStringA(section.c_str(), name.c_str(), std::to_string(std::get<float>(val)).c_str(), path.c_str());
            } else if (std::holds_alternative<std::string>(val)) {
                WritePrivateProfileStringA(section.c_str(), name.c_str(), std::get<std::string>(val).c_str(), path.c_str());
            } else if (std::holds_alternative<ImVec2>(val)) {
                ImVec2 v = std::get<ImVec2>(val);
                WritePrivateProfileStringA(section.c_str(), (name + "_X").c_str(), std::to_string(v.x).c_str(), path.c_str());
                WritePrivateProfileStringA(section.c_str(), (name + "_Y").c_str(), std::to_string(v.y).c_str(), path.c_str());
            } else if (std::holds_alternative<ImVec4>(val)) {
                ImVec4 v = std::get<ImVec4>(val);
                WritePrivateProfileStringA(section.c_str(), (name + "_X").c_str(), std::to_string(v.x).c_str(), path.c_str());
                WritePrivateProfileStringA(section.c_str(), (name + "_Y").c_str(), std::to_string(v.y).c_str(), path.c_str());
                WritePrivateProfileStringA(section.c_str(), (name + "_Z").c_str(), std::to_string(v.z).c_str(), path.c_str());
                WritePrivateProfileStringA(section.c_str(), (name + "_W").c_str(), std::to_string(v.w).c_str(), path.c_str());
            }
        }
    }

    void LoadSettings()
    {
        Initialize();
        std::string path = GetIniPath();

        for (auto& [key, val] : ConfigMap) {
            size_t dot = key.find('.');
            std::string section = (dot != std::string::npos) ? key.substr(0, dot) : "General";
            std::string name = (dot != std::string::npos) ? key.substr(dot + 1) : key;

            char buf[512];
            if (std::holds_alternative<bool>(val)) {
                std::get<bool>(val) = GetPrivateProfileIntA(section.c_str(), name.c_str(), std::get<bool>(val), path.c_str()) != 0;
            } else if (std::holds_alternative<int>(val)) {
                std::get<int>(val) = GetPrivateProfileIntA(section.c_str(), name.c_str(), std::get<int>(val), path.c_str());
            } else if (std::holds_alternative<float>(val)) {
                GetPrivateProfileStringA(section.c_str(), name.c_str(), std::to_string(std::get<float>(val)).c_str(), buf, sizeof(buf), path.c_str());
                try { std::get<float>(val) = std::stof(buf); } catch (...) {}
            } else if (std::holds_alternative<std::string>(val)) {
                GetPrivateProfileStringA(section.c_str(), name.c_str(), std::get<std::string>(val).c_str(), buf, sizeof(buf), path.c_str());
                std::get<std::string>(val) = std::string(buf);
            } else if (std::holds_alternative<ImVec2>(val)) {
                ImVec2& v = std::get<ImVec2>(val);
                GetPrivateProfileStringA(section.c_str(), (name + "_X").c_str(), std::to_string(v.x).c_str(), buf, sizeof(buf), path.c_str());
                try { v.x = std::stof(buf); } catch (...) {}
                GetPrivateProfileStringA(section.c_str(), (name + "_Y").c_str(), std::to_string(v.y).c_str(), buf, sizeof(buf), path.c_str());
                try { v.y = std::stof(buf); } catch (...) {}
            } else if (std::holds_alternative<ImVec4>(val)) {
                ImVec4& v = std::get<ImVec4>(val);
                GetPrivateProfileStringA(section.c_str(), (name + "_X").c_str(), std::to_string(v.x).c_str(), buf, sizeof(buf), path.c_str());
                try { v.x = std::stof(buf); } catch (...) {}
                GetPrivateProfileStringA(section.c_str(), (name + "_Y").c_str(), std::to_string(v.y).c_str(), buf, sizeof(buf), path.c_str());
                try { v.y = std::stof(buf); } catch (...) {}
                GetPrivateProfileStringA(section.c_str(), (name + "_Z").c_str(), std::to_string(v.z).c_str(), buf, sizeof(buf), path.c_str());
                try { v.z = std::stof(buf); } catch (...) {}
                GetPrivateProfileStringA(section.c_str(), (name + "_W").c_str(), std::to_string(v.w).c_str(), buf, sizeof(buf), path.c_str());
                try { v.w = std::stof(buf); } catch (...) {}
            }
        }
        
        Localization::CurrentLanguage = (Language)std::get<int>(ConfigMap["Misc.Language"]);
    }
}
