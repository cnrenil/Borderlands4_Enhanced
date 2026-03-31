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

    ImU32 ToOverlayColor(const ImVec4& color, float alphaScale = 1.0f)
    {
        ImVec4 copy = color;
        copy.w *= alphaScale;
        return ImGui::ColorConvertFloat4ToU32(copy);
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
    const ImVec4 glow = ConfigManager::Color("Misc.ThemeGlow");
    const float horizontalPadding = 18.0f;
    const float chipPaddingX = 14.0f;
    const float chipGap = 4.0f;
    const float maxAllowedWidth = (std::max)(260.0f, displaySize.x - 32.0f);
    const float desiredWidth = (maxLabelWidth + chipPaddingX * 2.0f) + (horizontalPadding * 2.0f) + 18.0f;
    const float windowWidth = (std::clamp)(desiredWidth, 210.0f, maxAllowedWidth);
    const float chipHeight = 28.0f;
    const float rowsHeight =
        static_cast<float>(activeLabels.size()) * chipHeight +
        static_cast<float>((std::max)(static_cast<int>(activeLabels.size()) - 1, 0)) * chipGap;
    const float windowX = 16.0f;
    const float windowY = 30.0f;
    const float contentTopPadding = 6.0f;
    const float contentBottomPadding = 8.0f;
    const float windowHeight = 12.0f + rowsHeight + contentBottomPadding;
    const ImVec2 windowSize(windowWidth, windowHeight);

    ImGui::SetNextWindowPos(ImVec2(windowX, windowY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::Begin(
        Localization::T("ACTIVE_FEATURES_LIST"),
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove);

    ImGui::SetCursorPos(ImVec2(horizontalPadding, contentTopPadding));

    const float rainbowTime = static_cast<float>(ImGui::GetTime()) * 2.2f;
    for (size_t i = 0; i < activeLabels.size(); ++i)
    {
        const char* label = activeLabels[i];
        const ImVec4 rainbowColor = GetRainbowTextColor(rainbowTime + (static_cast<float>(i) * 0.55f));
        const ImVec2 chipPos = ImGui::GetCursorScreenPos();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 chipSize(windowWidth - horizontalPadding * 2.0f, chipHeight);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float pulse = 0.5f + 0.5f * std::sinf(rainbowTime + static_cast<float>(i) * 0.42f);

        drawList->AddRectFilled(
            chipPos,
            ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
            ToOverlayColor(ImVec4(1.0f, 1.0f, 1.0f, 0.060f)),
            14.0f);
        drawList->AddRect(
            ImVec2(chipPos.x - 1.5f, chipPos.y - 1.5f),
            ImVec2(chipPos.x + chipSize.x + 1.5f, chipPos.y + chipSize.y + 1.5f),
            ToOverlayColor(glow, 0.14f + pulse * 0.10f),
            14.0f,
            0,
            3.0f);
        drawList->AddRect(
            chipPos,
            ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
            ToOverlayColor(ImVec4(glow.x, glow.y, glow.z, 0.22f + pulse * 0.10f)),
            14.0f,
            0,
            1.0f);
        drawList->AddCircleFilled(
            ImVec2(chipPos.x + 14.0f, chipPos.y + chipSize.y * 0.5f),
            4.0f,
            ToOverlayColor(glow, 0.90f));
        drawList->AddText(
            ImVec2(chipPos.x + 26.0f, chipPos.y + (chipSize.y - textSize.y) * 0.5f),
            ToOverlayColor(rainbowColor, 0.96f),
            label);

        ImGui::Dummy(ImVec2(chipSize.x, chipHeight + (i + 1 < activeLabels.size() ? chipGap : 0.0f)));
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
