#include "pch.h"

extern std::atomic<int> g_PresentCount;

namespace
{
    std::atomic<bool> g_LoggedReticleCanvas{ false };
    std::atomic<bool> g_LoggedReticleImGui{ false };

    bool IsOTSAdsActive()
    {
        if (!ConfigManager::B("Player.ThirdPerson") && !ConfigManager::B("Player.OverShoulder"))
            return false;
        if (!GVars.Character || !GVars.Character->IsA(AOakCharacter::StaticClass()))
            return false;
        const AOakCharacter* oakChar = static_cast<AOakCharacter*>(GVars.Character);
        return static_cast<uint8>(oakChar->ZoomState.State) != 0;
    }

    ImVec2 GetCustomReticleScreenPos()
    {
        if (!GVars.PlayerController || !GVars.PlayerController->PlayerCameraManager)
            return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

        const FMinimalViewInfo& cameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
        const FVector camLoc = cameraPOV.Location;
        const FVector camFwd = Utils::FRotatorToVector(cameraPOV.Rotation);
        const FVector aimPoint = camLoc + (camFwd * 50000.0f);

        FVector2D screen{};
        if (GVars.PlayerController->ProjectWorldLocationToScreen(aimPoint, &screen, true))
            return ImVec2(static_cast<float>(screen.X), static_cast<float>(screen.Y));

        return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    }

    struct RenderState
    {
        uint32_t LastUpdateFrame = 0xFFFFFFFF;
        uint32_t LastCanvasTickPresentFrame = 0xFFFFFFFF;
        uint64_t LastCanvasTickTimeMs = 0;
        uint64_t LastHudSignalTimeMs = 0;
        bool LoggedCanvasTickActive = false;
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

    void RunGameThreadRenderTick(UCanvas* Canvas, bool bAllowDraw)
    {
        const int currentFrame = g_PresentCount.load();
        auto& renderState = GetRenderState();
        if (renderState.LastCanvasTickPresentFrame == static_cast<uint32_t>(currentFrame))
            return;

        Utils::SetCurrentCanvas(Canvas);
        renderState.LastCanvasTickPresentFrame = static_cast<uint32_t>(currentFrame);
        renderState.LastCanvasTickTimeMs = GetTickCount64();

        if (Canvas && !renderState.LoggedCanvasTickActive)
        {
            renderState.LoggedCanvasTickActive = true;
            LOG_INFO("CanvasTick", "GameThreadCanvasTick active. Canvas=%p", Canvas);
        }

        if (!TryAutoSetVariablesForRender())
        {
            Logger::LogThrottled(Logger::Level::Warning, "CanvasTick", 1000, "GameThread canvas tick: AutoSetVariables exception, skipping frame");
            Utils::SetCurrentCanvas(nullptr);
            return;
        }

        // Camera and gameplay-facing updates should run on the game thread.
        Cheats::UpdateCamera();

        if (Utils::bIsInGame)
        {
            Logger::LogThrottled(Logger::Level::Debug, "CanvasTick", 10000, "Cheats::GameThreadCanvasTick: HUD/game-thread logic active");
            if (ConfigManager::B("Player.ESP")) Cheats::UpdateESP();
            if (ConfigManager::B("Aimbot.Enabled"))
            {
                Cheats::Aimbot();
                if (!ConfigManager::B("Aimbot.RequireKeyHeld"))
                {
                    Cheats::AimbotHotkey();
                }
            }

            Cheats::UpdateMovement();
            Cheats::UpdateWeapon();
            Cheats::EnforcePersistence();
            Cheats::ChangeGameRenderSettings();
        }

        if (bAllowDraw && Utils::bIsInGame && ConfigManager::B("Aimbot.Enabled"))
        {
            if (ConfigManager::B("Aimbot.DrawFOV"))
                Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));

            if (Cheats::bHasAimbotTarget && ConfigManager::B("Aimbot.DrawArrow"))
                Utils::DrawSnapLine(Cheats::AimbotTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
        }

        if (bAllowDraw && Utils::bIsInGame)
            Cheats::RenderESP();

        if (bAllowDraw && Utils::bIsInGame)
            Cheats::DrawReticle();

        if (bAllowDraw && Utils::bIsInGame)
            Cheats::RenderEnabledOptions();

        Utils::SetCurrentCanvas(nullptr);
    }
}

void Cheats::GameThreadCanvasTick(UCanvas* Canvas)
{
    auto& renderState = GetRenderState();
    renderState.LastHudSignalTimeMs = GetTickCount64();
    RunGameThreadRenderTick(Canvas, Canvas != nullptr);
}

void Cheats::DrawReticle()
{
    if (!IsOTSAdsActive())
        return;

    const ImVec2 center = GetCustomReticleScreenPos();
    const ImU32 outer = IM_COL32(0, 0, 0, 180);
    const ImU32 inner = IM_COL32(255, 255, 255, 255);
    const float gap = 3.0f;
    const float len = 8.0f;

    if (GUI::Draw::ResolveBackend() == GUI::Draw::Backend::UCanvas)
    {
        if (!g_LoggedReticleCanvas.exchange(true))
        {
            LOG_INFO("DrawPath", "DrawReticle using UCanvas path.");
        }
    }
    else if (!g_LoggedReticleImGui.exchange(true))
    {
        LOG_WARN("DrawPath", "DrawReticle using ImGui fallback path (Canvas unavailable).");
    }

    GUI::Draw::Circle(center, 2.0f, outer, 12, 2.0f);
    GUI::Draw::Circle(center, 1.0f, inner, 12, 1.5f);
    GUI::Draw::Line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), outer, 2.5f);
    GUI::Draw::Line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), outer, 2.5f);
    GUI::Draw::Line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), outer, 2.5f);
    GUI::Draw::Line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), outer, 2.5f);
    GUI::Draw::Line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), inner, 1.2f);
    GUI::Draw::Line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), inner, 1.2f);
    GUI::Draw::Line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), inner, 1.2f);
    GUI::Draw::Line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), inner, 1.2f);
}

void Cheats::Render()
{
    const int currentFrame = g_PresentCount.load();
    auto& renderState = GetRenderState();

    // Fallback path if the HUD canvas tick stalls for long enough.
    if (renderState.LastUpdateFrame != currentFrame)
    {
        renderState.LastUpdateFrame = static_cast<uint32_t>(currentFrame);
        HotkeyManager::Update();

        const uint64_t nowMs = GetTickCount64();
        const uint64_t lastHudActivityMs = renderState.LastHudSignalTimeMs != 0
            ? renderState.LastHudSignalTimeMs
            : renderState.LastCanvasTickTimeMs;
        if (lastHudActivityMs == 0 || (nowMs - lastHudActivityMs) > 500)
        {
            Logger::LogThrottled(Logger::Level::Warning, "Render", 3000, "HUD canvas tick timeout, using Present fallback for logic-only update");
            RunGameThreadRenderTick(nullptr, false);
        }
    }

    // Main Menu remains on ImGui.
    GUI::RenderMenu();
}
