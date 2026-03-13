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

			if (Behavior->IsA(SDK::UWeaponBehavior_FireProjectile::StaticClass())) {
				SDK::UWeaponBehavior_FireProjectile* FP = static_cast<SDK::UWeaponBehavior_FireProjectile*>(Behavior);
				if (ConfigManager::B("Weapon.InstantHit")) {
					FP->ProjectileSpeedScale.Value = ConfigManager::F("Weapon.ProjectileSpeedMultiplier");
				}
				else {
					FP->ProjectileSpeedScale.Value = FP->ProjectileSpeedScale.BaseValue;
				}
			}
			
			if (Behavior->IsA(SDK::UWeaponBehavior_Fire::StaticClass())) {
				SDK::UWeaponBehavior_Fire* FB = static_cast<SDK::UWeaponBehavior_Fire*>(Behavior);
				if (ConfigManager::B("Weapon.RapidFire")) {
					FB->firerate.Value = 999.0f * ConfigManager::F("Weapon.FireRate");
				}
				else {
					FB->firerate.Value = FB->firerate.BaseValue;
				}

				if (ConfigManager::B("Weapon.NoRecoil")) {
					FB->RecoilScale.Value = FB->RecoilScale.BaseValue * (1.0f - ConfigManager::F("Weapon.RecoilReduction"));
				}
				else {
					FB->RecoilScale.Value = FB->RecoilScale.BaseValue;
				}

				if (ConfigManager::B("Weapon.NoSpread")) {
					FB->spread.Value = 0.0f;
				}
				else {
					FB->spread.Value = FB->spread.BaseValue;
				}
			}

			if (Behavior->IsA(SDK::UWeaponBehavior_Reload::StaticClass())) {
				SDK::UWeaponBehavior_Reload* RB = static_cast<SDK::UWeaponBehavior_Reload*>(Behavior);
				if (ConfigManager::B("Weapon.InstantReload")) {
					// Keep a tiny positive duration to avoid edge cases with zero-time reload.
					RB->ReloadTime.Value = 0.01f;
					RB->MinReloadTime.Value = 0.01f;
				}
				else {
					RB->ReloadTime.Value = RB->ReloadTime.BaseValue;
					RB->MinReloadTime.Value = RB->MinReloadTime.BaseValue;
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
