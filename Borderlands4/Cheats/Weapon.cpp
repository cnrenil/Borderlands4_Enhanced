#include "pch.h"


void Cheats::WeaponModifiers()
{
	if (!GVars.Character || !Utils::bIsInGame) return;
	for (uint8 i = 0; i < 4; i++) {
		SDK::AWeapon* weapon = SDK::UWeaponStatics::GetWeapon(GVars.Character, i);
		if (!weapon) continue;

		int32_t NumBehaviors = weapon->behaviors.Num();
		for (int b = 0; b < NumBehaviors; b++) {
			SDK::UWeaponBehavior* Behavior = weapon->behaviors[b];
			if (!Behavior) continue;

			if (Behavior->IsA(SDK::UWeaponBehavior_FireProjectile::StaticClass()) && ConfigManager::B("Weapon.InstantHit")) {
				static_cast<SDK::UWeaponBehavior_FireProjectile*>(Behavior)->ProjectileSpeedScale.Value = ConfigManager::F("Weapon.ProjectileSpeedMultiplier");
			}
			
			if (Behavior->IsA(SDK::UWeaponBehavior_Fire::StaticClass())) {
				SDK::UWeaponBehavior_Fire* FB = static_cast<SDK::UWeaponBehavior_Fire*>(Behavior);
				if (ConfigManager::B("Weapon.RapidFire")) FB->firerate.Value = 999.0f * ConfigManager::F("Weapon.FireRate");
				if (ConfigManager::B("Weapon.NoRecoil")) {
					FB->RecoilScale.Value = FB->RecoilScale.BaseValue * (1.0f - ConfigManager::F("Weapon.RecoilReduction"));
				}
			}
		}
	}
}

void Cheats::UpdateWeapon()
{
    Cheats::WeaponModifiers();
    Cheats::SilentAimHoming();
    Cheats::TriggerBot();
}

bool Cheats::HandleWeaponEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
    if (ConfigManager::B("Weapon.InstantReload") && Function->GetName() == "ServerStartReloading") {
        SDK::AWeapon* weapon = (SDK::AWeapon*)Object;
        if (weapon && weapon->IsA(SDK::AWeapon::StaticClass())) {
            struct ReloadParams { uint8 UseModeIndex; uint8 Flags; }*p = (ReloadParams*)Params;
            int32 MaxAmmo = SDK::UWeaponStatics::GetMaxLoadedAmmo(weapon, p->UseModeIndex);
            
            weapon->ClientSetLoadedAmmo(p->UseModeIndex, MaxAmmo);
            weapon->ClientStopReloading();
            weapon->ServerInterruptReloadToUse(MaxAmmo);
            
            if (oProcessEvent) oProcessEvent(Object, Function, Params);
            return true; 
        }
    }
    return false;
}
