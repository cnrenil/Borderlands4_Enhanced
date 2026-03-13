#include "pch.h"

void Cheats::Render()
{
    // 1. Logic-based visualizations (Draw FOV, Snaplines)
    if (ConfigManager::B("Aimbot.Enabled"))
    {
        if (ConfigManager::B("Aimbot.DrawFOV"))
            Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));

        if (bHasAimbotTarget && ConfigManager::B("Aimbot.DrawArrow"))
            Utils::DrawSnapLine(AimbotTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
    }

    if (ConfigManager::B("Weapon.HomingProjectiles") && bHasSilentAimTarget && ConfigManager::B("Aimbot.DrawArrow"))
    {
        Utils::DrawSnapLine(SilentAimTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
    }

    // 2. World-based visualizations (ESP)
    RenderESP();

    // 3. UI Overlays (Active Features List)
    RenderEnabledOptions();

    // 4. Main Menu
    GUI::RenderMenu();
}
