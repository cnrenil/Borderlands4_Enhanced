#include "pch.h"

namespace
{
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

        const float perimeter = (size.x + size.y) * 2.0f - 24.0f;
        if (perimeter <= 1.0f)
            return;

        const float segmentLength = (std::min)(perimeter * 0.22f, 120.0f);
        const float travel = std::fmod(static_cast<float>(ImGui::GetTime()) * 120.0f, perimeter);
        auto pointAtDistance = [&](float distance) -> ImVec2
        {
            const float left = pos.x + 12.0f;
            const float top = pos.y + 12.0f;
            const float right = max.x - 12.0f;
            const float bottom = max.y - 12.0f;
            const float topLen = right - left;
            const float rightLen = bottom - top;
            const float bottomLen = topLen;
            const float leftLen = rightLen;

            if (distance < topLen) return ImVec2(left + distance, top);
            distance -= topLen;
            if (distance < rightLen) return ImVec2(right, top + distance);
            distance -= rightLen;
            if (distance < bottomLen) return ImVec2(right - distance, bottom);
            distance -= bottomLen;
            return ImVec2(left, bottom - (std::min)(distance, leftLen));
        };

        const ImVec2 start = pointAtDistance(travel);
        const ImVec2 end = pointAtDistance(std::fmod(travel + segmentLength, perimeter));
        const ImU32 marqueeColor = IM_COL32(
            (accent >> IM_COL32_R_SHIFT) & 0xFF,
            (accent >> IM_COL32_G_SHIFT) & 0xFF,
            (accent >> IM_COL32_B_SHIFT) & 0xFF,
            188);

        if ((start.x == end.x) || (start.y == end.y))
        {
            drawList->AddLine(start, end, marqueeColor, 2.0f);
        }
        else
        {
            const ImVec2 corner = pointAtDistance(std::fmod(travel + (segmentLength * 0.5f), perimeter));
            drawList->AddLine(start, corner, marqueeColor, 2.0f);
            drawList->AddLine(corner, end, marqueeColor, 2.0f);
        }
    }
}

void Cheats::RenderEnabledOptions()
{
	if (!ConfigManager::B("Misc.RenderOptions")) return;

    std::vector<const char*> activeKeys;
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

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const ImVec2 windowSize(240.0f, 52.0f + (float)activeKeys.size() * 22.0f);
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - windowSize.x - 24.0f, 30.0f), ImGuiCond_Always);
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
    ImGui::SetCursorPos(ImVec2(16.0f, 34.0f));

    for (const char* key : activeKeys)
    {
        ImGui::BulletText("%s", Localization::T(key));
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
