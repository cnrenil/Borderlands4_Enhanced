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

	float GetInputDoubleClickTimeMs()
	{
		const SDK::UInputSettings* inputSettings = SDK::UInputSettings::GetDefaultObj();
		if (!inputSettings) return 220.0f;

		const float ms = inputSettings->DoubleClickTime * 1000.0f;
		return std::clamp(ms, 80.0f, 450.0f);
	}

	float GetWeaponTapIntervalMs(const SDK::AWeapon* weapon, bool bIsAutomaticWeapon)
	{
		float bestFireRate = 0.0f;
		if (weapon)
		{
			for (int i = 0; i < weapon->behaviors.Num(); i++)
			{
				SDK::UWeaponBehavior* behavior = weapon->behaviors[i];
				if (!behavior || !behavior->IsA(SDK::UWeaponBehavior_Fire::StaticClass()))
					continue;

				const auto* fireBehavior = static_cast<SDK::UWeaponBehavior_Fire*>(behavior);
				bestFireRate = (std::max)(bestFireRate, fireBehavior->firerate.Value);
				bestFireRate = (std::max)(bestFireRate, fireBehavior->firerate.BaseValue);
			}
		}

		float intervalMs = bIsAutomaticWeapon ? 95.0f : 140.0f;
		if (bestFireRate > 0.05f)
		{
			intervalMs = 1000.0f / bestFireRate;
		}

		if (bIsAutomaticWeapon)
			return std::clamp(intervalMs, 45.0f, 180.0f);
		return std::clamp(intervalMs, 65.0f, 260.0f);
	}
}

void Cheats::TriggerBot()
{
	static bool bIsFiringUnderControl = false;
	static uint64_t NextTapTimeMs = 0;
	static uint64_t PendingReleaseTimeMs = 0;
	extern std::atomic<int> g_PresentCount;

	auto ReleaseControl = [&]()
		{
			if (bIsFiringUnderControl)
			{
				SendMouseLeftUp();
				bIsFiringUnderControl = false;
			}
			NextTapTimeMs = 0;
			PendingReleaseTimeMs = 0;
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
	const uint64_t nowMs = GetTickCount64();
	if (bIsFiringUnderControl && PendingReleaseTimeMs > 0 && nowMs >= PendingReleaseTimeMs)
	{
		SendMouseLeftUp();
		bIsFiringUnderControl = false;
		PendingReleaseTimeMs = 0;
		Cheats::bTriggerSuppressMouseInput.store(false);
	}

	float tapIntervalMs = GetWeaponTapIntervalMs(activeWeapon, bIsAutomaticWeapon);
	// Avoid sending taps faster than engine input pacing can reliably process.
	const float enginePacingFloorMs = GetInputDoubleClickTimeMs() * 0.5f;
	tapIntervalMs = (std::max)(tapIntervalMs, enginePacingFloorMs);
	const uint64_t tapInterval = static_cast<uint64_t>(tapIntervalMs);
	const uint64_t holdMs = static_cast<uint64_t>(std::clamp(tapIntervalMs * 0.18f, 8.0f, 24.0f));

	if (!bIsFiringUnderControl && nowMs >= NextTapTimeMs)
	{
		SendMouseLeftDown();
		bIsFiringUnderControl = true;
		PendingReleaseTimeMs = nowMs + holdMs;
		NextTapTimeMs = nowMs + tapInterval;
		Cheats::bTriggerSuppressMouseInput.store(true);
	}
}

void Cheats::TriggerHotkey()
{
	if (!Utils::bIsInGame) return;
	extern std::atomic<int> g_PresentCount;
	g_LastTriggerHotkeyFrame.store(g_PresentCount.load());
}
