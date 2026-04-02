#include "pch.h"

extern std::atomic<int> g_PresentCount;

namespace Features
{
	namespace
	{
		struct TriggerState
		{
			AActor* CurrentTarget = nullptr;
			std::atomic<int> LastHotkeyFrame{ -1 };
		};

		TriggerState& GetTriggerState()
		{
			static TriggerState state;
			return state;
		}

		AWeapon* GetLikelyActiveWeapon()
		{
			APawn* controlledPawn = nullptr;
			if (GVars.PlayerController && Utils::IsValidActor(GVars.PlayerController))
				controlledPawn = GVars.PlayerController->Pawn;

			if (controlledPawn && Utils::IsValidActor(controlledPawn) && controlledPawn->IsA(AOakVehicle::StaticClass()))
			{
				auto* vehicle = reinterpret_cast<AOakVehicle*>(controlledPawn);

				for (const auto& slot : vehicle->ActiveWeapons.Slots)
				{
					AWeapon* weapon = slot.Weapon;
					if (weapon && Utils::IsValidActor(weapon))
						return weapon;
				}

				for (AWeapon* weapon : vehicle->VehicleWeapons)
				{
					if (weapon && Utils::IsValidActor(weapon))
						return weapon;
				}
			}

			AActor* weaponUser = Utils::GetSelfActor();
			if (!weaponUser || !Utils::IsValidActor(weaponUser)) return nullptr;

			if (weaponUser->IsA(AOakCharacter::StaticClass()))
			{
				auto* oakCharacter = reinterpret_cast<AOakCharacter*>(weaponUser);
				AWeapon* firstValid = nullptr;

				for (const auto& slot : oakCharacter->ActiveWeapons.Slots)
				{
					AWeapon* weapon = slot.Weapon;
					if (!weapon || !Utils::IsValidActor(weapon)) continue;
					if (!firstValid) firstValid = weapon;

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

			for (uint8 i = 0; i < 4; i++)
			{
				AWeapon* weapon = UWeaponStatics::GetWeapon(weaponUser, i);
				if (weapon && Utils::IsValidActor(weapon)) return weapon;
			}

			return nullptr;
		}

		bool IsWeaponAutomatic(const AWeapon* weapon)
		{
			if (!weapon) return false;

			bool bFoundFireBehavior = false;
			int32_t automaticBurstCount = 1;

			for (int i = 0; i < weapon->behaviors.Num(); i++)
			{
				UWeaponBehavior* behavior = weapon->behaviors[i];
				if (!behavior) continue;

				if (behavior->IsA(UWeaponBehavior_FireBeam::StaticClass()))
				{
					return true;
				}

				if (behavior->IsA(UWeaponBehavior_Fire::StaticClass()))
				{
					bFoundFireBehavior = true;
					const auto* fireBehavior = static_cast<UWeaponBehavior_Fire*>(behavior);
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
			const UInputSettings* inputSettings = UInputSettings::GetDefaultObj();
			if (!inputSettings) return 220.0f;

			const float ms = inputSettings->DoubleClickTime * 1000.0f;
			return std::clamp(ms, 80.0f, 450.0f);
		}

		float GetWeaponTapIntervalMs(const AWeapon* weapon, bool bIsAutomaticWeapon)
		{
			float bestFireRate = 0.0f;
			if (weapon)
			{
				for (int i = 0; i < weapon->behaviors.Num(); i++)
				{
					UWeaponBehavior* behavior = weapon->behaviors[i];
					if (!behavior || !behavior->IsA(UWeaponBehavior_Fire::StaticClass()))
						continue;

					const auto* fireBehavior = static_cast<UWeaponBehavior_Fire*>(behavior);
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

		void TriggerHotkeyFeature()
		{
			GetTriggerState().LastHotkeyFrame.store(g_PresentCount.load());
		}

		void TriggerBotFeature()
		{
			static bool bIsFiringUnderControl = false;
			static uint64_t NextTapTimeMs = 0;
			static uint64_t PendingReleaseTimeMs = 0;

			auto ReleaseControl = [&]()
				{
					if (bIsFiringUnderControl)
					{
						Utils::SendMouseLeftUp();
						bIsFiringUnderControl = false;
					}
					NextTapTimeMs = 0;
					PendingReleaseTimeMs = 0;
					Features::Data::bTriggerSuppressMouseInput.store(false);
				};

			auto& triggerState = GetTriggerState();
			triggerState.CurrentTarget = nullptr;
			if (!ConfigManager::B("Trigger.Enabled") || !GVars.PlayerController || !Utils::GetSelfActor())
			{
				ReleaseControl();
				return;
			}
			if (ImGui::GetIO().WantCaptureMouse)
			{
				ReleaseControl();
				return;
			}

			const TargetSelectionResult targetResult = Utils::AcquireTarget(
				GVars.PlayerController,
				5.0f,
				ConfigManager::F("Aimbot.MinDistance"),
				ConfigManager::F("Aimbot.MaxDistance"),
				true,
				ConfigManager::S("Aimbot.Bone"),
				ConfigManager::B("Trigger.TargetAll"),
				ConfigManager::I("Aimbot.TargetMode")
			);
			triggerState.CurrentTarget = targetResult.Target;

			const int currentFrame = g_PresentCount.load();
			const bool bHotkeyHeld = (triggerState.LastHotkeyFrame.load() == currentFrame);
			const bool bCanFire = triggerState.CurrentTarget &&
				(!ConfigManager::B("Trigger.RequireKeyHeld") || bHotkeyHeld);

			if (!bCanFire)
			{
				ReleaseControl();
				return;
			}

			AWeapon* activeWeapon = GetLikelyActiveWeapon();
			const bool bIsAutomaticWeapon = IsWeaponAutomatic(activeWeapon);
			const uint64_t nowMs = GetTickCount64();
			if (bIsFiringUnderControl && PendingReleaseTimeMs > 0 && nowMs >= PendingReleaseTimeMs)
			{
				Utils::SendMouseLeftUp();
				bIsFiringUnderControl = false;
				PendingReleaseTimeMs = 0;
				Features::Data::bTriggerSuppressMouseInput.store(false);
			}

			float tapIntervalMs = GetWeaponTapIntervalMs(activeWeapon, bIsAutomaticWeapon);
			const float enginePacingFloorMs = GetInputDoubleClickTimeMs() * 0.5f;
			tapIntervalMs = (std::max)(tapIntervalMs, enginePacingFloorMs);
			const uint64_t tapInterval = static_cast<uint64_t>(tapIntervalMs);
			const uint64_t holdMs = static_cast<uint64_t>(std::clamp(tapIntervalMs * 0.18f, 8.0f, 24.0f));

			if (!bIsFiringUnderControl && nowMs >= NextTapTimeMs)
			{
				Utils::SendMouseLeftDown();
				bIsFiringUnderControl = true;
				PendingReleaseTimeMs = nowMs + holdMs;
				NextTapTimeMs = nowMs + tapInterval;
				Features::Data::bTriggerSuppressMouseInput.store(true);
			}
		}
	}

	void RegisterTriggerFeature()
	{
		HotkeyManager::Register("Trigger.Key", "TRIGGER_KEY", ImGuiKey_MouseX2, []()
		{
			TriggerHotkeyFeature();
		}, true);

		Core::Scheduler::RegisterGameplayTickCallback("Trigger", []()
		{
			TriggerBotFeature();
		});
	}
}
