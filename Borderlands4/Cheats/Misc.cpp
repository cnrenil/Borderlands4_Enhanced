#include "pch.h"

namespace
{
    ImVec4 GetRainbowTextColor(float phase)
    {
        const float r = 0.5f + (0.5f * std::sin(phase));
        const float g = 0.5f + (0.5f * std::sin(phase + 2.0943951f));
        const float b = 0.5f + (0.5f * std::sin(phase + 4.1887902f));
        const float lift = 0.18f;
        return ImVec4(
            lift + (r * (1.0f - lift)),
            lift + (g * (1.0f - lift)),
            lift + (b * (1.0f - lift)),
            0.98f);
    }

    void DrawOverlayHudChrome(const ImVec2& pos, const ImVec2& size, ImU32 accent)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = 20.0f;
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        drawList->AddRectFilled(
            ImVec2(pos.x + 10.0f, pos.y + 14.0f),
            ImVec2(max.x + 10.0f, max.y + 14.0f),
            IM_COL32(0, 0, 0, 24),
            rounding + 4.0f);

        drawList->AddRectFilledMultiColor(
            pos,
            max,
            IM_COL32(255, 255, 255, 16),
            IM_COL32(255, 255, 255, 10),
            IM_COL32(255, 255, 255, 6),
            IM_COL32(255, 255, 255, 12));

        drawList->AddRect(pos, max, IM_COL32(255, 255, 255, 24), rounding, 0, 1.0f);
        drawList->AddRect(
            ImVec2(pos.x + 1.0f, pos.y + 1.0f),
            ImVec2(max.x - 1.0f, max.y - 1.0f),
            IM_COL32(255, 255, 255, 10),
            rounding - 1.0f,
            0,
            1.0f);

        drawList->AddLine(
            ImVec2(pos.x + 18.0f, pos.y + 48.0f),
            ImVec2(pos.x + size.x - 18.0f, pos.y + 48.0f),
            IM_COL32(255, 255, 255, 14),
            1.0f);
    }
}

void Cheats::RenderEnabledOptions()
{
	if (!ConfigManager::B("Misc.RenderOptions")) return;

    std::vector<const char*> activeKeys;
    std::vector<const char*> activeLabels;
    auto addEntry = [&](bool enabled, const char* key)
    {
        if (enabled)
            activeKeys.push_back(key);
    };

    addEntry(ConfigManager::B("Player.GodMode"), "GODMODE");
    addEntry(ConfigManager::B("Player.InfAmmo"), "INF_AMMO");
    addEntry(ConfigManager::B("Player.InfGrenades"), "INF_GRENADES");
    addEntry(ConfigManager::B("Player.InfVehicleBoost"), "INF_VEHICLE_BOOST");
    addEntry(ConfigManager::B("Player.InfGlideStamina"), "INF_GLIDE_STAMINA");
    addEntry(ConfigManager::B("Aimbot.Enabled"), "AIMBOT");
    addEntry(ConfigManager::B("Player.ESP"), "ESP");

    if (activeKeys.empty())
        return;

    activeLabels.reserve(activeKeys.size());
    float maxLabelWidth = 0.0f;
    for (const char* key : activeKeys)
    {
        const char* label = Localization::T(key);
        activeLabels.push_back(label);
        maxLabelWidth = (std::max)(maxLabelWidth, ImGui::CalcTextSize(label).x);
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float titleWidth = ImGui::CalcTextSize(Localization::T("ACTIVE_FEATURES")).x;
    const float horizontalPadding = 14.0f;
    const float maxAllowedWidth = (std::max)(210.0f, displaySize.x - 32.0f);
    const float desiredWidth = (std::max)(titleWidth, maxLabelWidth) + (horizontalPadding * 2.0f) + 8.0f;
    const float windowWidth = (std::clamp)(desiredWidth, 210.0f, maxAllowedWidth);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 windowSize(windowWidth, 56.0f + (static_cast<float>(activeLabels.size()) * rowHeight));
    const float windowX = 16.0f;
    ImGui::SetNextWindowPos(ImVec2(windowX, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::Begin(
        Localization::T("ACTIVE_FEATURES_LIST"),
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove);

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    DrawOverlayHudChrome(pos, size, IM_COL32(102, 214, 213, 220));

    ImGui::SetCursorPos(ImVec2(16.0f, 12.0f));
    ImGui::TextColored(ImVec4(0.86f, 0.92f, 0.95f, 0.94f), "%s", Localization::T("ACTIVE_FEATURES"));
    ImGui::SetCursorPos(ImVec2(16.0f, 38.0f));

    const float rainbowTime = static_cast<float>(ImGui::GetTime()) * 2.2f;
    for (size_t i = 0; i < activeLabels.size(); ++i)
    {
        const char* label = activeLabels[i];
        ImGui::SetCursorPosX(16.0f);
        const ImVec4 rainbowColor = GetRainbowTextColor(rainbowTime + (static_cast<float>(i) * 0.55f));
        ImGui::TextColored(rainbowColor, "%s", label);
    }
	ImGui::End();
}

void Cheats::KillEnemies() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Kill);
}

void Cheats::GiveLevels() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Levels);
}

void Cheats::SpawnItems() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Items);
}

void Cheats::TogglePlayersOnly() {
    SDK::UWorld* World = Utils::GetWorldSafe();
    if (World && World->GameState && World->GameState->IsA(SDK::AGbxGameState::StaticClass())) {
        static_cast<SDK::AGbxGameState*>(World->GameState)->bRepPlayersOnly = ConfigManager::B("Player.PlayersOnly");
    }
}

void Cheats::AddCurrency(const std::string& type, int amount)
{
    if (!GVars.PlayerController || !GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass())) return;
    SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
    if (!OakPC->CurrencyManager) return;

    for (int i = 0; i < OakPC->CurrencyManager->currencies.Num(); i++) {
        auto& cur = OakPC->CurrencyManager->currencies[i];
        if (cur.type.Name.ToString().find(type) != std::string::npos) {
            OakPC->Server_AddCurrency(cur.type, amount);
            break;
        }
    }
}

void Cheats::ClearGroundItems()
{
    if (!GVars.World || !GVars.Level) return;
    SDK::FVector farLoc = { 100000.f, 100000.f, -100000.f };

    Utils::ForEachLevelActor(GVars.Level, [&](SDK::AActor* Actor)
    {
        if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass()))
            Actor->K2_SetActorLocation(farLoc, false, nullptr, false);
        return true;
    });
}

void Cheats::SetExperienceLevel(int32 xpAmount)
{
    SDK::UWorld* World = Utils::GetWorldSafe();
    if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

    SDK::APlayerController* PC = World->OwningGameInstance->LocalPlayers[0]->PlayerController;
    if (!PC || !PC->IsA(SDK::AOakPlayerController::StaticClass())) return;

    SDK::AOakPlayerController* OakController = static_cast<SDK::AOakPlayerController*>(PC);
    SDK::AOakPlayerState* PS = OakController->GetOakPlayerState();

    if (PS)
    {
        if (PS->ExperienceState.Num() > 0)
        {
            PS->ExperienceState[0].ExperienceLevel = xpAmount;
        }
    }

    SDK::AOakCharacter* localChar = static_cast<SDK::AOakCharacter*>(OakController->Character);
    if (localChar && localChar->IsA(SDK::AOakCharacter::StaticClass()))
    {
        localChar->BroadcastLevelUp(xpAmount);
    }
}
