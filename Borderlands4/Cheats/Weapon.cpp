#include "pch.h"

namespace
{
	static std::unordered_map<SDK::AWeapon*, float> g_LastInstantReloadFixTime;
	static std::unordered_map<SDK::AWeapon*, float> g_ReloadStuckSince;

	struct PendingReloadUnstickSwap
	{
		SDK::AWeapon* OriginalWeapon = nullptr;
		int OriginalSlot = -1;
		int TempSlot = -1;
		float SwitchBackTime = 0.0f;
	};

	struct OriginalSwapTiming
	{
		float EquipTimeValue = 0.0f;
		float EquipTimeBase = 0.0f;
		float PutDownTimeValue = 0.0f;
		float PutDownTimeBase = 0.0f;
	};

	struct OriginalAmmoCost
	{
		int32 ShotAmmoCostValue = 0;
		int32 ShotAmmoCostBase = 0;
	};

	static std::unordered_map<SDK::AOakCharacter*, PendingReloadUnstickSwap> g_PendingReloadUnstickSwap;
	static std::unordered_map<SDK::AWeapon*, OriginalSwapTiming> g_OriginalSwapTiming;
	static std::unordered_map<SDK::UWeaponBehavior_Fire*, OriginalAmmoCost> g_OriginalAmmoCost;

	int FindWeaponSlot(SDK::AOakCharacter* OakChar, SDK::AWeapon* Weapon)
	{
		if (!OakChar || !Weapon) return -1;
		const int32 slotCount = OakChar->ActiveWeapons.Slots.Num();
		for (int32 i = 0; i < slotCount; ++i)
		{
			SDK::AWeapon* slotWeapon = OakChar->ActiveWeapons.Slots[i].Weapon;
			if (slotWeapon == Weapon) return i;
		}
		return -1;
	}

	int FindAlternateSlot(SDK::AOakCharacter* OakChar, int CurrentSlot)
	{
		if (!OakChar) return -1;
		const int32 slotCount = OakChar->ActiveWeapons.Slots.Num();
		for (int32 i = 0; i < slotCount; ++i)
		{
			if (i == CurrentSlot) continue;
			SDK::AWeapon* slotWeapon = OakChar->ActiveWeapons.Slots[i].Weapon;
			if (slotWeapon && Utils::IsValidActor(slotWeapon)) return i;
		}
		return -1;
	}

	void SwitchToSlot(SDK::AOakCharacter* OakChar, int Slot)
	{
		if (!OakChar || Slot < 0 || Slot >= OakChar->ActiveWeapons.Slots.Num()) return;
		SDK::AWeapon* slotWeapon = OakChar->ActiveWeapons.Slots[Slot].Weapon;
		if (!slotWeapon || !Utils::IsValidActor(slotWeapon)) return;

		OakChar->SwitchToAIWeapon(Slot, true);
		OakChar->ServerSetCurrentWeapon(
			slotWeapon,
			Slot,
			SDK::EWeaponPutDownType::Instant,
			SDK::EWeaponEquipType::Instant,
			Slot);
	}

	void ForceClearCharacterWeaponLocks(SDK::AOakCharacter* OakChar, int Slot)
	{
		if (!OakChar) return;
		const SDK::FName reason = SDK::UKismetStringLibrary::Conv_StringToName(L"guest");
		const int32 channelsMask = 0x7FFFFFFF;
		OakChar->ServerStopWeaponActions(Slot, channelsMask, true);
		OakChar->ServerUnlockWeaponActions(reason, channelsMask);
	}

	void ProcessPendingReloadUnstickSwap(SDK::AOakCharacter* OakChar)
	{
		if (!OakChar) return;
		auto it = g_PendingReloadUnstickSwap.find(OakChar);
		if (it == g_PendingReloadUnstickSwap.end()) return;

		const float now = (float)ImGui::GetTime();
		if (now < it->second.SwitchBackTime) return;

		if (it->second.OriginalSlot >= 0)
		{
			SwitchToSlot(OakChar, it->second.OriginalSlot);
		}
		g_PendingReloadUnstickSwap.erase(it);
	}

	void TryScheduleReloadUnstickSwap(SDK::AOakCharacter* OakChar, SDK::AWeapon* Weapon)
	{
		if (!OakChar || !Weapon) return;
		if (g_PendingReloadUnstickSwap.find(OakChar) != g_PendingReloadUnstickSwap.end()) return;

		const int originalSlot = FindWeaponSlot(OakChar, Weapon);
		if (originalSlot < 0) return;
		const int tempSlot = FindAlternateSlot(OakChar, originalSlot);
		ForceClearCharacterWeaponLocks(OakChar, originalSlot);
		if (tempSlot < 0) {
			// No alternate weapon: still force a same-slot re-equip to break reload lock state.
			SwitchToSlot(OakChar, originalSlot);
			return;
		}

		SwitchToSlot(OakChar, tempSlot);

		PendingReloadUnstickSwap pending{};
		pending.OriginalWeapon = Weapon;
		pending.OriginalSlot = originalSlot;
		pending.TempSlot = tempSlot;
		pending.SwitchBackTime = (float)ImGui::GetTime() + 0.06f;
		g_PendingReloadUnstickSwap[OakChar] = pending;
	}

	void ApplyInstantSwapTiming(SDK::AWeapon* Weapon, bool bEnable)
	{
		if (!Weapon) return;

		auto it = g_OriginalSwapTiming.find(Weapon);
		if (bEnable)
		{
			if (it == g_OriginalSwapTiming.end())
			{
				OriginalSwapTiming original{};
				original.EquipTimeValue = Weapon->EquipTime.Value;
				original.EquipTimeBase = Weapon->EquipTime.BaseValue;
				original.PutDownTimeValue = Weapon->PutDownTime.Value;
				original.PutDownTimeBase = Weapon->PutDownTime.BaseValue;
				g_OriginalSwapTiming[Weapon] = original;
			}

			Weapon->EquipTime.Value = 0.01f;
			Weapon->EquipTime.BaseValue = 0.01f;
			Weapon->PutDownTime.Value = 0.01f;
			Weapon->PutDownTime.BaseValue = 0.01f;
		}
		else if (it != g_OriginalSwapTiming.end())
		{
			Weapon->EquipTime.Value = it->second.EquipTimeValue;
			Weapon->EquipTime.BaseValue = it->second.EquipTimeBase;
			Weapon->PutDownTime.Value = it->second.PutDownTimeValue;
			Weapon->PutDownTime.BaseValue = it->second.PutDownTimeBase;
			g_OriginalSwapTiming.erase(it);
		}
	}

	void ApplyNoAmmoConsume(SDK::UWeaponBehavior_Fire* FireBehavior, bool bEnable)
	{
		if (!FireBehavior) return;

		auto it = g_OriginalAmmoCost.find(FireBehavior);
		if (bEnable)
		{
			if (it == g_OriginalAmmoCost.end())
			{
				OriginalAmmoCost original{};
				original.ShotAmmoCostValue = FireBehavior->ShotAmmoCost.Value;
				original.ShotAmmoCostBase = FireBehavior->ShotAmmoCost.BaseValue;
				g_OriginalAmmoCost[FireBehavior] = original;
			}

			FireBehavior->ShotAmmoCost.Value = 0;
			FireBehavior->ShotAmmoCost.BaseValue = 0;
		}
		else if (it != g_OriginalAmmoCost.end())
		{
			FireBehavior->ShotAmmoCost.Value = it->second.ShotAmmoCostValue;
			FireBehavior->ShotAmmoCost.BaseValue = it->second.ShotAmmoCostBase;
			g_OriginalAmmoCost.erase(it);
		}
	}
}

void Cheats::WeaponModifiers()
{
	if (!GVars.Character || !Utils::bIsInGame) return;
	SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
	ProcessPendingReloadUnstickSwap(OakChar);

	if (!ConfigManager::B("Weapon.InstantReload"))
	{
		g_LastInstantReloadFixTime.clear();
		g_ReloadStuckSince.clear();
		g_PendingReloadUnstickSwap.clear();
	}
	const bool bInstantSwap = ConfigManager::B("Weapon.InstantSwap");
	const bool bNoAmmoConsume = ConfigManager::B("Weapon.NoAmmoConsume");

	for (uint8 i = 0; i < 4; i++) {
		SDK::AWeapon* weapon = SDK::UWeaponStatics::GetWeapon(GVars.Character, i);
		if (!weapon) continue;
		ApplyInstantSwapTiming(weapon, bInstantSwap);

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
				ApplyNoAmmoConsume(FB, bNoAmmoConsume);

				if (ConfigManager::B("Weapon.RapidFire")) {
					const float baseFireRate = FB->firerate.BaseValue > 0.0f ? FB->firerate.BaseValue : FB->firerate.Value;
					FB->firerate.Value = baseFireRate * ConfigManager::F("Weapon.FireRate");
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

					// BL4 stock reload path may read taped reload timing.
					if (RB->IsA(SDK::UWeaponBehavior_StockReload::StaticClass())) {
						SDK::UWeaponBehavior_StockReload* SRB = static_cast<SDK::UWeaponBehavior_StockReload*>(RB);
						SRB->TapedReloadTime.Value = 0.01f;
					}
				}
				else {
					RB->ReloadTime.Value = RB->ReloadTime.BaseValue;
					RB->MinReloadTime.Value = RB->MinReloadTime.BaseValue;

					if (RB->IsA(SDK::UWeaponBehavior_StockReload::StaticClass())) {
						SDK::UWeaponBehavior_StockReload* SRB = static_cast<SDK::UWeaponBehavior_StockReload*>(RB);
						SRB->TapedReloadTime.Value = SRB->TapedReloadTime.BaseValue;
					}
				}
			}
		}

		if (ConfigManager::B("Weapon.InstantReload")) {
			const std::string state = weapon->CurrentState.ToString();
			const bool bReloading = state.find("Reload") != std::string::npos || state.find("reload") != std::string::npos;

			if (bReloading) {
				const float now = (float)ImGui::GetTime();
				float& lastFixTime = g_LastInstantReloadFixTime[weapon];
				uint8 useMode = weapon->CurrentUseModeIndex;
				if (useMode > 7) useMode = 0; // guard weird transient values

				int32 maxAmmo = SDK::UWeaponStatics::GetMaxLoadedAmmo(weapon, useMode);
				if (maxAmmo <= 0) continue;

				const int32 loadedAmmo = SDK::UWeaponStatics::GetLoadedAmmo(weapon, useMode);
				if ((now - lastFixTime) >= 0.08f) {
					lastFixTime = now;

					if (loadedAmmo < maxAmmo) {
						weapon->ClientSetLoadedAmmo(useMode, maxAmmo);
						weapon->ClientRefillAmmo(useMode, maxAmmo - loadedAmmo);
					}

					// Keep weapon state machine and UI in sync after forced ammo refill.
					weapon->ServerInterruptReloadToUse(maxAmmo);
					weapon->ServerAmmoGivenToInterruptedReload();
					weapon->ServerSendAmmoState(useMode);
					weapon->ServerRestartAutoUse();
					weapon->ClientUnlock();
					weapon->ServerUnlock();
					weapon->ServerEquipInterruptible();
					weapon->OnRep_VisibleAmmoState();
					weapon->ServerStopUsing(0, true, 0);
					weapon->ServerStopModeSwitch(0, useMode);
					weapon->CurrentState = SDK::UKismetStringLibrary::Conv_StringToName(L"Active");
					weapon->PendingUseModeIndex = useMode;
					weapon->ServerStartUsing(0, 0);
				}

				// Stock reload has a dedicated replicated state used by weapon-status UI.
				for (int b = 0; b < NumBehaviors; b++) {
					SDK::UWeaponBehavior* Behavior = weapon->behaviors[b];
					if (!Behavior) continue;
					if (Behavior->IsA(SDK::UWeaponBehavior_StockReload::StaticClass())) {
						SDK::UWeaponBehavior_StockReload* SRB = static_cast<SDK::UWeaponBehavior_StockReload*>(Behavior);
						SRB->ClientReloadState = 0;
						SRB->bResumingReload = false;
						SRB->OnRep_ClientReloadState();
					}
				}

				float& stuckSince = g_ReloadStuckSince[weapon];
				if (stuckSince <= 0.0f) stuckSince = now;
				if ((now - stuckSince) > 0.10f) {
					TryScheduleReloadUnstickSwap(OakChar, weapon);
					stuckSince = now;
				}
			}
			else {
				g_LastInstantReloadFixTime.erase(weapon);
				g_ReloadStuckSince.erase(weapon);
			}
		}
	}
}

void Cheats::UpdateWeapon()
{
    Cheats::WeaponModifiers();
    Cheats::TriggerBot();
}

bool Cheats::HandleWeaponEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
    const std::string functionName = Function->GetName();

    if (ConfigManager::B("Weapon.InstantReload") && functionName == "ServerStartReloading") {
        SDK::AWeapon* weapon = (SDK::AWeapon*)Object;
        if (weapon && weapon->IsA(SDK::AWeapon::StaticClass())) {
            struct ReloadParams { uint8 UseModeIndex; uint8 Flags; }*p = (ReloadParams*)Params;
            if (!p) return false;
            int32 MaxAmmo = SDK::UWeaponStatics::GetMaxLoadedAmmo(weapon, p->UseModeIndex);
            if (MaxAmmo <= 0) return false;
            
            weapon->ClientSetLoadedAmmo(p->UseModeIndex, MaxAmmo);
            weapon->ServerInterruptReloadToUse(MaxAmmo);
            weapon->ServerAmmoGivenToInterruptedReload();
            weapon->ServerSendAmmoState(p->UseModeIndex);
            weapon->ServerRestartAutoUse();
            weapon->ClientUnlock();
            weapon->ServerUnlock();
            weapon->ServerEquipInterruptible();
            weapon->OnRep_VisibleAmmoState();
            weapon->CurrentState = SDK::UKismetStringLibrary::Conv_StringToName(L"Active");
            weapon->PendingUseModeIndex = p->UseModeIndex;
            weapon->ServerStartUsing(0, 0);
            
            if (oProcessEvent) oProcessEvent(Object, Function, Params);
            return true; 
        }
    }
    return false;
}
