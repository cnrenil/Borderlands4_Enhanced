#include "pch.h"

void Cheats::Render()
{
    // --- Logic Update (Moved from PostRender for stability) ---
    // This ensures that even if our PostRender hook fails, the logic still ticks
    static uint32_t LastUpdateFrame = 0xFFFFFFFF;
    extern std::atomic<int> g_PresentCount;
    int currentFrame = g_PresentCount.load();

    if (LastUpdateFrame != currentFrame)
    {
        LastUpdateFrame = currentFrame;

        // Perform essential updates
        GVars.AutoSetVariables();
        HotkeyManager::Update();

        if (Utils::bIsInGame)
        {
            Logger::LogThrottled(Logger::Level::Debug, "Render", 10000, "Cheats::Render: Logic thread active (not loading)");
            if (ConfigManager::B("Player.ESP")) UpdateESP();
            if (ConfigManager::B("Aimbot.Enabled")) Aimbot();

            UpdateMovement();
            UpdateWeapon();
            UpdateCamera();

            EnforcePersistence();
            ChangeGameRenderSettings();
        }
    }

    // --- Visualizations ---
    // 1. Logic-based visualizations (Draw FOV, Snaplines)
    if (Utils::bIsInGame && ConfigManager::B("Aimbot.Enabled"))
    {
        if (ConfigManager::B("Aimbot.DrawFOV"))
            Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));

        if (bHasAimbotTarget && ConfigManager::B("Aimbot.DrawArrow"))
            Utils::DrawSnapLine(AimbotTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
    }

    if (Utils::bIsInGame && ConfigManager::B("Weapon.HomingProjectiles") && bHasSilentAimTarget && ConfigManager::B("Aimbot.DrawArrow"))
    {
        Utils::DrawSnapLine(SilentAimTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
    }

    // 2. World-based visualizations (ESP)
    if (Utils::bIsInGame)
        RenderESP();

    // 3. UI Overlays (Active Features List)
    if (Utils::bIsInGame)
        RenderEnabledOptions();

    // 4. Main Menu
    GUI::RenderMenu();
}
