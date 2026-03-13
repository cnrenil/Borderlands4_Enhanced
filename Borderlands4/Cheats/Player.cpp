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
	static bool bOneShotAppliedAfterInjection = false;

	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.Character) {
		return;
	}

	// Permanent state enforcement (every frame)
	GVars.Character->bCanBeDamaged = !ConfigManager::B("Player.GodMode");
	Cheats::SetGameSpeed(ConfigManager::F("Player.GameSpeed"));
	Cheats::TogglePlayersOnly();

	// One-shot activation (only once after injection, on first valid in-game instance)
	if (!bOneShotAppliedAfterInjection) {
		bOneShotAppliedAfterInjection = true;
		using namespace ConfigManager;

		// Re-trigger toggle-style cheats if they are enabled in config
		if (B("Player.Demigod")) Cheats::ToggleDemigod();
		if (B("Player.InfAmmo")) Cheats::InfiniteAmmo();
		if (B("Player.NoTarget")) Cheats::ToggleNoTarget();
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
