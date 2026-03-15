#include "pch.h"

namespace
{
    struct RenderState
    {
        uint32_t LastUpdateFrame = 0xFFFFFFFF;
        uint32_t LastCanvasTickPresentFrame = 0xFFFFFFFF;
        uint64_t LastCanvasTickTimeMs = 0;
    };

    RenderState& GetRenderState()
    {
        static RenderState state;
        return state;
    }

    void AutoSetVariablesLocked()
    {
        std::scoped_lock GVarsLock(gGVarsMutex);
        GVars.AutoSetVariables();
    }

    bool TryAutoSetVariablesForRender()
    {
        __try
        {
            AutoSetVariablesLocked();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GVars.Reset();
            Utils::bIsLoading = true;
            Utils::bIsInGame = false;
            return false;
        }
    }
}

void Cheats::GameThreadCanvasTick(UCanvas* Canvas)
{
    extern std::atomic<int> g_PresentCount;
    const int currentFrame = g_PresentCount.load();
    auto& renderState = GetRenderState();
    if (renderState.LastCanvasTickPresentFrame == static_cast<uint32_t>(currentFrame))
        return;

    Utils::SetCurrentCanvas(Canvas);
    renderState.LastCanvasTickPresentFrame = static_cast<uint32_t>(currentFrame);
    renderState.LastCanvasTickTimeMs = GetTickCount64();

    if (!TryAutoSetVariablesForRender())
    {
        Logger::LogThrottled(Logger::Level::Warning, "CanvasTick", 1000, "GameThread canvas tick: AutoSetVariables exception, skipping frame");
        Utils::SetCurrentCanvas(nullptr);
        return;
    }

    // Camera and gameplay-facing updates should run on the game thread.
    UpdateCamera();

    if (Utils::bIsInGame)
    {
        Logger::LogThrottled(Logger::Level::Debug, "CanvasTick", 10000, "Cheats::GameThreadCanvasTick: HUD/game-thread logic active");
        if (ConfigManager::B("Player.ESP")) UpdateESP();
        if (ConfigManager::B("Aimbot.Enabled"))
        {
            Aimbot();
            if (!ConfigManager::B("Aimbot.RequireKeyHeld"))
            {
                AimbotHotkey();
            }
        }

        UpdateMovement();
        UpdateWeapon();
        EnforcePersistence();
        ChangeGameRenderSettings();
    }

    if (Utils::bIsInGame && ConfigManager::B("Aimbot.Enabled"))
    {
        if (ConfigManager::B("Aimbot.DrawFOV"))
            Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));

        if (bHasAimbotTarget && ConfigManager::B("Aimbot.DrawArrow"))
            Utils::DrawSnapLine(AimbotTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
    }

    if (Utils::bIsInGame)
        RenderESP();

    if (Utils::bIsInGame)
        RenderEnabledOptions();

    Utils::SetCurrentCanvas(nullptr);
}

void Cheats::Render()
{
    extern std::atomic<int> g_PresentCount;
    const int currentFrame = g_PresentCount.load();
    auto& renderState = GetRenderState();

    // Fallback path if the HUD canvas tick stalls for long enough.
    if (renderState.LastUpdateFrame != currentFrame)
    {
        renderState.LastUpdateFrame = static_cast<uint32_t>(currentFrame);
        HotkeyManager::Update();

        const uint64_t nowMs = GetTickCount64();
        if (renderState.LastCanvasTickTimeMs == 0 || (nowMs - renderState.LastCanvasTickTimeMs) > 500)
        {
            Logger::LogThrottled(Logger::Level::Warning, "Render", 3000, "HUD canvas tick timeout, using Present fallback for logic");
            GameThreadCanvasTick(nullptr);
        }
    }

    // Main Menu remains on ImGui.
    GUI::RenderMenu();
}
