#include "pch.h"

static AActor* CurrentTriggerTarget = nullptr;
static std::atomic<int> g_LastTriggerHotkeyFrame{ -1 };

namespace
{
	void SendMouseLeftDown()
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		SendInput(1, &input, sizeof(INPUT));
	}

	void SendMouseLeftUp()
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
		SendInput(1, &input, sizeof(INPUT));
	}

	SDK::AWeapon* GetLikelyActiveWeapon()
	{
		APawn* controlledPawn = nullptr;
		if (GVars.PlayerController && Utils::IsValidActor(GVars.PlayerController))
			controlledPawn = GVars.PlayerController->Pawn;

		if (controlledPawn && Utils::IsValidActor(controlledPawn) && controlledPawn->IsA(SDK::AOakVehicle::StaticClass()))
		{
			auto* vehicle = reinterpret_cast<SDK::AOakVehicle*>(controlledPawn);

			for (const auto& slot : vehicle->ActiveWeapons.Slots)
			{
				SDK::AWeapon* weapon = slot.Weapon;
				if (weapon && Utils::IsValidActor(weapon))
					return weapon;
			}

			for (SDK::AWeapon* weapon : vehicle->VehicleWeapons)
			{
				if (weapon && Utils::IsValidActor(weapon))
					return weapon;
			}
		}

		AActor* weaponUser = Utils::GetSelfActor();
		if (!weaponUser || !Utils::IsValidActor(weaponUser)) return nullptr;

		// Prefer the active weapon slots from OakCharacter when available.
		if (weaponUser->IsA(SDK::AOakCharacter::StaticClass()))
		{
			auto* oakCharacter = reinterpret_cast<SDK::AOakCharacter*>(weaponUser);
			SDK::AWeapon* firstValid = nullptr;

			for (const auto& slot : oakCharacter->ActiveWeapons.Slots)
			{
				SDK::AWeapon* weapon = slot.Weapon;
				if (!weapon || !Utils::IsValidActor(weapon)) continue;
				if (!firstValid) firstValid = weapon;

				// Heuristic: active weapon state is generally not "None"/holstered.
				const std::string state = weapon->CurrentState.ToString();
				if (!state.empty() &&
					state != "None" &&
					state.find("Holster") == std::string::npos &&
					state.find("PutDown") == std::string::npos)
				{
					return weapon;
				}
			}

			if (firstValid) return firstValid;
		}

		// Fallback to user weapon slots.
		for (uint8 i = 0; i < 4; i++)
		{
			SDK::AWeapon* weapon = SDK::UWeaponStatics::GetWeapon(weaponUser, i);
			if (weapon && Utils::IsValidActor(weapon)) return weapon;
		}

		return nullptr;
	}

	bool IsWeaponAutomatic(const SDK::AWeapon* weapon)
	{
		if (!weapon) return false;

		bool bFoundFireBehavior = false;
		int32_t automaticBurstCount = 1;

		for (int i = 0; i < weapon->behaviors.Num(); i++)
		{
			SDK::UWeaponBehavior* behavior = weapon->behaviors[i];
			if (!behavior) continue;

			if (behavior->IsA(SDK::UWeaponBehavior_FireBeam::StaticClass()))
			{
				return true;
			}

			if (behavior->IsA(SDK::UWeaponBehavior_Fire::StaticClass()))
			{
				bFoundFireBehavior = true;
				const auto* fireBehavior = static_cast<SDK::UWeaponBehavior_Fire*>(behavior);
				automaticBurstCount = (std::max)(automaticBurstCount, fireBehavior->AutomaticBurstCount.Value);
				automaticBurstCount = (std::max)(automaticBurstCount, fireBehavior->AutomaticBurstCount.BaseValue);

				if (fireBehavior->BurstFireDelay.Value > 0.0f || fireBehavior->BurstFireDelay.BaseValue > 0.0f)
				{
					return true;
				}
			}
		}

		if (!bFoundFireBehavior) return true;
		return automaticBurstCount > 1;
	}
}

void Cheats::TriggerBot()
{
	static bool bIsFiringUnderControl = false;
	static bool bPendingSemiRelease = false;
	static uint64_t LastSemiTapTimeMs = 0;
	extern std::atomic<int> g_PresentCount;

	auto ReleaseControl = [&]()
		{
			if (bIsFiringUnderControl)
			{
				SendMouseLeftUp();
				bIsFiringUnderControl = false;
			}
			bPendingSemiRelease = false;
			Cheats::bTriggerSuppressMouseInput.store(false);
		};

	CurrentTriggerTarget = nullptr;
	if (!ConfigManager::B("Trigger.Enabled") || !Utils::bIsInGame || !GVars.PlayerController || !Utils::GetSelfActor())
	{
		ReleaseControl();
		return;
	}
	if (ImGui::GetIO().WantCaptureMouse)
	{
		ReleaseControl();
		return;
	}

	// Target acquisition
	CurrentTriggerTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		5.0f, // Narrow FOV for triggerbot
		true, // Must have Line Of Sight
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Trigger.TargetAll")
	);

	const int currentFrame = g_PresentCount.load();
	const bool bHotkeyHeld = (g_LastTriggerHotkeyFrame.load() == currentFrame);
	const bool bCanFire = CurrentTriggerTarget &&
		(!ConfigManager::B("Trigger.RequireKeyHeld") || bHotkeyHeld);

	if (!bCanFire)
	{
		ReleaseControl();
		return;
	}

	SDK::AWeapon* activeWeapon = GetLikelyActiveWeapon();
	const bool bIsAutomaticWeapon = IsWeaponAutomatic(activeWeapon);

	if (bIsAutomaticWeapon)
	{
		if (!bIsFiringUnderControl)
		{
			SendMouseLeftDown();
			bIsFiringUnderControl = true;
		}
		bPendingSemiRelease = false;
		Cheats::bTriggerSuppressMouseInput.store(true);
		return;
	}

	// Semi-auto: tap at a controlled rate instead of holding.
	if (bPendingSemiRelease && bIsFiringUnderControl)
	{
		SendMouseLeftUp();
		bIsFiringUnderControl = false;
		bPendingSemiRelease = false;
		Cheats::bTriggerSuppressMouseInput.store(false);
	}

	const float fireRateMultiplier = (std::max)(0.1f, ConfigManager::F("Weapon.FireRate"));
	const uint64_t nowMs = GetTickCount64();
	float tapInterval = 150.0f / fireRateMultiplier;
	tapInterval = (std::max)(35.0f, tapInterval);
	tapInterval = (std::min)(220.0f, tapInterval);
	const uint64_t tapIntervalMs = static_cast<uint64_t>(tapInterval);

	if (!bIsFiringUnderControl && (nowMs - LastSemiTapTimeMs) >= tapIntervalMs)
	{
		SendMouseLeftDown();
		bIsFiringUnderControl = true;
		bPendingSemiRelease = true;
		LastSemiTapTimeMs = nowMs;
		Cheats::bTriggerSuppressMouseInput.store(true);
	}
}

void Cheats::TriggerHotkey()
{
	if (!Utils::bIsInGame) return;
	extern std::atomic<int> g_PresentCount;
	g_LastTriggerHotkeyFrame.store(g_PresentCount.load());
}
