#include "pch.h"

void Cheats::RenderEnabledOptions()
{
	if (!ConfigManager::B("Misc.RenderOptions")) return;
	float Hue = fmodf((float)ImGui::GetTime() * 0.2f, 1.0f);
	ImVec4 Color = ImColor::HSV(Hue, 1.f, 1.f);
	ImGui::Begin(Localization::T("ACTIVE_FEATURES_LIST"), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetWindowPos(ImVec2(10, 30));
	if (ConfigManager::B("Player.GodMode")) ImGui::TextColored(Color, Localization::T("GODMODE"));
	if (ConfigManager::B("Player.InfAmmo")) ImGui::TextColored(Color, Localization::T("INF_AMMO"));
	if (ConfigManager::B("Player.InfVehicleBoost")) ImGui::TextColored(Color, Localization::T("INF_VEHICLE_BOOST"));
	if (ConfigManager::B("Player.InfGlideStamina")) ImGui::TextColored(Color, Localization::T("INF_GLIDE_STAMINA"));
	if (ConfigManager::B("Aimbot.Enabled")) ImGui::TextColored(Color, Localization::T("AIMBOT"));
	if (ConfigManager::B("Player.ESP")) ImGui::TextColored(Color, Localization::T("ESP"));
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

    for (int i = 0; i < GVars.Level->Actors.Num(); i++) {
        SDK::AActor* Actor = GVars.Level->Actors[i];
        if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass())) {
            Actor->K2_SetActorLocation(farLoc, false, nullptr, false);
        }
    }
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
