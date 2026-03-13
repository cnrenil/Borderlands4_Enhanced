#include "pch.h"

void Cheats::ToggleGodMode() { ConfigManager::B("Player.GodMode") = !ConfigManager::B("Player.GodMode"); }
void Cheats::InfiniteAmmo() { 
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Loaded);
}

void Cheats::ToggleDemigod() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Demigod);
}

void Cheats::EnforcePersistence()
{
	static SDK::ACharacter* LastCharacter = nullptr;
	static SDK::UWorld* LastWorld = nullptr;

	if (Utils::bIsLoading || !GVars.PlayerController || !GVars.Character) {
		LastCharacter = nullptr;
		LastWorld = nullptr;
		return;
	}

	bool bInstanceChanged = false;
	if (LastCharacter != GVars.Character || LastWorld != GVars.World) {
		bInstanceChanged = true;
		LastCharacter = GVars.Character;
		LastWorld = GVars.World;
	}

	// 1. Permanent State Enforcement (Every Frame)
	GVars.Character->bCanBeDamaged = !ConfigManager::B("Player.GodMode");

	// 2. One-Shot Activation (On Map Change / Respawn)
	if (bInstanceChanged) {
		using namespace ConfigManager;

		// Re-trigger toggle-style cheats if they are enabled in config
		if (B("Player.Demigod")) Cheats::ToggleDemigod();
		if (B("Player.InfAmmo")) Cheats::InfiniteAmmo();
		if (B("Player.NoTarget")) Cheats::ToggleNoTarget();
		if (F("Player.GameSpeed") != 1.0f) Cheats::SetGameSpeed(F("Player.GameSpeed"));
        
        // Ensure other persistent states are pushed to game systems
        if (B("Player.PlayersOnly")) Cheats::TogglePlayersOnly();
	}
}

void Cheats::ToggleNoTarget()
{
	if (!GVars.PlayerController || !GVars.Character) return;
	SDK::UGbxTargetingFunctionLibrary::LockTargetableByAI(GVars.Character, SDK::UKismetStringLibrary::Conv_StringToName(L"guest"), ConfigManager::B("Player.NoTarget"), ConfigManager::B("Player.NoTarget"));
}

void Cheats::SetGameSpeed(float Speed)
{
	SDK::UWorld* World = Utils::GetWorldSafe();
	if (World && World->PersistentLevel) World->PersistentLevel->WorldSettings->TimeDilation = Speed;
}
