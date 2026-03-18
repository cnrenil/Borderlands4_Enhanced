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
        const FRotator camRot = cameraPOV.Rotation;
        const FVector camFwd = Utils::FRotatorToVector(camRot);
        const FVector aimPoint = camLoc + (camFwd * 50000.0f);

        FVector2D screen{};
        if (Utils::ProjectWorldLocationToScreen(aimPoint, screen, true))
            return ImVec2(static_cast<float>(screen.X), static_cast<float>(screen.Y));

        return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    }

    struct RenderState
    {
        uint32_t LastUpdateFrame = 0xFFFFFFFF;
    };

    RenderState& GetRenderState()
    {
        static RenderState state;
        return state;
    }

    void AutoSetVariablesLocked()
    {
        SDK::UWorld* currentWorld = Utils::GetWorldSafe();
        bool shouldReleaseShadowCamera = false;
        bool hadShadowCamera = false;
        {
            std::scoped_lock GVarsLock(gGVarsMutex);
            hadShadowCamera = GVars.CameraActor != nullptr;
            shouldReleaseShadowCamera = hadShadowCamera && GVars.World != nullptr && GVars.World != currentWorld;
        }

        if (shouldReleaseShadowCamera)
        {
            Cheats::ShutdownCamera();
        }

        if (!shouldReleaseShadowCamera && hadShadowCamera)
        {
            SDK::APlayerController* currentPlayerController = nullptr;
            SDK::ACharacter* currentCharacter = nullptr;
            if (currentWorld)
            {
                currentPlayerController = Utils::GetPlayerController();
                if (currentPlayerController && !IsBadReadPtr(currentPlayerController, sizeof(void*)) && currentPlayerController->VTable)
                {
                    currentCharacter = currentPlayerController->Character;
                }
            }

            if (!currentWorld || !currentPlayerController || !currentCharacter)
            {
                Logger::LogThrottled(Logger::Level::Debug, "Camera", 1000, "Render detected world/gameplay teardown before GVars refresh, releasing shadow camera");
                Cheats::ShutdownCamera();
            }
        }

        {
            std::scoped_lock GVarsLock(gGVarsMutex);
            GVars.AutoSetVariables();
        }
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

    void RunGameThreadRenderTick(bool bAllowDraw)
    {
        const int currentFrame = g_PresentCount.load();
        auto& renderState = GetRenderState();
        if (renderState.LastUpdateFrame == static_cast<uint32_t>(currentFrame))
            return;

        Utils::SetCurrentCanvas(nullptr);
        renderState.LastUpdateFrame = static_cast<uint32_t>(currentFrame);

        if (!TryAutoSetVariablesForRender())
        {
            Logger::LogThrottled(Logger::Level::Warning, "Render", 1000, "Present render tick: AutoSetVariables exception, skipping frame");
            Utils::SetCurrentCanvas(nullptr);
            return;
        }

        // Camera and gameplay-facing updates should run on the game thread.
        Cheats::UpdateCamera();
        SilentAimHooks::Tick();

        if (Utils::bIsInGame)
        {
            Logger::LogThrottled(Logger::Level::Debug, "Render", 10000, "Present render tick: gameplay logic active");
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

void Cheats::DrawReticle()
{
    if (!ConfigManager::B("Misc.Reticle") || !IsOTSAdsActive())
        return;

    const ImVec2 offset = ConfigManager::Vec2("Misc.ReticlePosition");
    const ImVec2 baseCenter = GetCustomReticleScreenPos();
    const ImVec2 center(baseCenter.x + offset.x, baseCenter.y + offset.y);
    const ImVec4 reticleColor = ConfigManager::Color("Misc.ReticleColor");
    const ImU32 inner = ImGui::ColorConvertFloat4ToU32(reticleColor);
    const ImU32 outer = IM_COL32(0, 0, 0, static_cast<int>(reticleColor.w * 180.0f));
    const float size = std::clamp(ConfigManager::F("Misc.ReticleSize"), 2.0f, 30.0f);
    const float gap = size * 0.45f;
    const float len = size * 1.2f;
    const float dotOuterRadius = (std::max)(1.5f, size * 0.32f);
    const float dotInnerRadius = (std::max)(1.0f, size * 0.16f);
    const bool drawCrosshair = ConfigManager::B("Misc.CrossReticle");

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

    GUI::Draw::Circle(center, dotOuterRadius, outer, 16, 2.0f);
    GUI::Draw::Circle(center, dotInnerRadius, inner, 16, 1.5f);

    if (!drawCrosshair)
        return;

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
    HotkeyManager::Update();
    RunGameThreadRenderTick(true);

    // Main Menu remains on ImGui.
    GUI::RenderMenu();
}
