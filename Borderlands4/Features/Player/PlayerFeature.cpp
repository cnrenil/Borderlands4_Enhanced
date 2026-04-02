#include "pch.h"
#include "PlayerFeature.h"

namespace Features::Player
{
	namespace
	{
		static SDK::UOakCharacterMovementComponent* g_LastGlideMoveComp = nullptr;
		static bool g_WasInfGlideStaminaOn = false;
		static float g_OriginalGlideCostValue = -1.0f;
		static float g_OriginalGlideCostBase = -1.0f;
		static float g_OriginalDashCostValue = -1.0f;
		static float g_OriginalDashCostBase = -1.0f;
		static double g_LastGlideRefreshTime = 0.0;

		float ResolveBase(float Original, float Current, float Fallback)
		{
			if (Original > 0.01f) return Original;
			if (Current > 0.01f) return Current;
			return Fallback;
		}

		SDK::AInventoryGadget* GetLocalInventoryGadget()
		{
			SDK::UWorld* world = Utils::GetWorldSafe();
			if (!world || !world->PersistentLevel || !GVars.Character) return nullptr;

			SDK::AInventoryGadget* firstGadget = nullptr;
			Utils::ForEachLevelActor(world->PersistentLevel, [&](SDK::AActor* actor)
			{
				if (!actor || !actor->IsA(SDK::AInventoryGadget::StaticClass())) return true;
				SDK::AInventoryGadget* gadget = static_cast<SDK::AInventoryGadget*>(actor);
				if (gadget->OwningCharacter == GVars.Character)
				{
					firstGadget = gadget;
					return false;
				}
				if (!firstGadget) firstGadget = gadget;
				return true;
			});
			return firstGadget;
		}

		void ApplyInfiniteGrenades()
		{
			if (!ConfigManager::B("Player.InfGrenades")) return;
			SDK::AInventoryGadget* gadget = GetLocalInventoryGadget();
			if (!gadget) return;

			gadget->CooldownTime.Value = 0.0f;
			gadget->CooldownTime.BaseValue = 0.0f;
			gadget->NumberOfCharges = 1000;
			gadget->MaxNumberOfCharges.Value = 1000;
			gadget->MaxNumberOfCharges.BaseValue = 1000;
		}

		void ApplyInfiniteGlideStamina()
		{
			SDK::AOakCharacter* oakChar = (GVars.Character && Utils::IsValidActor(GVars.Character))
				? reinterpret_cast<SDK::AOakCharacter*>(GVars.Character)
				: nullptr;

			SDK::UOakCharacterMovementComponent* move = (oakChar && oakChar->OakCharacterMovement) ? oakChar->OakCharacterMovement : nullptr;
			const bool enabled = ConfigManager::B("Player.InfGlideStamina");

			if (!enabled || !move)
			{
				if (g_WasInfGlideStaminaOn && g_LastGlideMoveComp)
				{
					if (g_OriginalGlideCostValue >= 0.0f)
						g_LastGlideMoveComp->VaultPowerCost_Glide.Value = g_OriginalGlideCostValue;
					if (g_OriginalGlideCostBase >= 0.0f)
						g_LastGlideMoveComp->VaultPowerCost_Glide.BaseValue = g_OriginalGlideCostBase;
					if (g_OriginalDashCostValue >= 0.0f)
						g_LastGlideMoveComp->VaultPowerCost_Dash.Value = g_OriginalDashCostValue;
					if (g_OriginalDashCostBase >= 0.0f)
						g_LastGlideMoveComp->VaultPowerCost_Dash.BaseValue = g_OriginalDashCostBase;
				}

				g_WasInfGlideStaminaOn = false;
				g_LastGlideMoveComp = move;
				return;
			}

			if (g_LastGlideMoveComp != move)
			{
				g_LastGlideMoveComp = move;
				g_OriginalGlideCostValue = -1.0f;
				g_OriginalGlideCostBase = -1.0f;
				g_OriginalDashCostValue = -1.0f;
				g_OriginalDashCostBase = -1.0f;
			}

			if (g_OriginalGlideCostValue < 0.0f) g_OriginalGlideCostValue = move->VaultPowerCost_Glide.Value;
			if (g_OriginalGlideCostBase < 0.0f) g_OriginalGlideCostBase = move->VaultPowerCost_Glide.BaseValue;
			if (g_OriginalDashCostValue < 0.0f) g_OriginalDashCostValue = move->VaultPowerCost_Dash.Value;
			if (g_OriginalDashCostBase < 0.0f) g_OriginalDashCostBase = move->VaultPowerCost_Dash.BaseValue;

			move->VaultPowerCost_Glide.Value = 0.0f;
			move->VaultPowerCost_Glide.BaseValue = 0.0f;
			move->VaultPowerCost_Dash.Value = 0.0f;
			move->VaultPowerCost_Dash.BaseValue = 0.0f;
			g_WasInfGlideStaminaOn = true;

			const double now = ImGui::GetTime();
			if ((now - g_LastGlideRefreshTime) > 0.2)
			{
				move->ClientOnVaultPowerNotDepleted();
				g_LastGlideRefreshTime = now;
			}
		}
	}

	void SetGodMode(bool enabled) { ConfigManager::B("Player.GodMode") = enabled; }

	void SetInfiniteAmmo(bool enabled) {
		if (enabled && GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Loaded);
	}

	void SetDemigod(bool enabled) {
		if (enabled && GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Demigod);
	}

	void SetNoTarget(bool enabled)
	{
		if (!GVars.PlayerController || !GVars.Character) return;
		SDK::UGbxTargetingFunctionLibrary::LockTargetableByAI(GVars.Character, SDK::UKismetStringLibrary::Conv_StringToName(L"guest"), enabled, enabled);
	}

	void SetInfiniteGrenades(bool enabled)
	{
		ConfigManager::B("Player.InfGrenades") = enabled;
	}

	void GiveLevels()
	{
		if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Levels);
	}

	void SetExperienceLevel(int32_t level)
	{
		SDK::UWorld* World = Utils::GetWorldSafe();
		if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

		SDK::APlayerController* PC = World->OwningGameInstance->LocalPlayers[0]->PlayerController;
		if (!PC || !PC->IsA(SDK::AOakPlayerController::StaticClass())) return;

		SDK::AOakPlayerController* OakController = static_cast<SDK::AOakPlayerController*>(PC);
		SDK::AOakPlayerState* PS = static_cast<SDK::AOakPlayerState*>(OakController->PlayerState);

		if (PS && PS->IsA(SDK::AOakPlayerState::StaticClass()))
		{
			if (PS->ExperienceState.Num() > 0)
			{
				PS->ExperienceState[0].ExperienceLevel = level;
			}
		}

		SDK::AOakCharacter* localChar = static_cast<SDK::AOakCharacter*>(OakController->Character);
		if (localChar && localChar->IsA(SDK::AOakCharacter::StaticClass()))
		{
			localChar->BroadcastLevelUp(level);
		}
	}

	void AddCurrency(const std::string& type, int amount)
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

	void Update()
	{
		ApplyInfiniteGrenades();
		ApplyInfiniteGlideStamina();
	}
}

namespace Features
{
	void RegisterPlayerFeature()
	{
		Core::Scheduler::RegisterGameplayTickCallback("Player", []()
		{
			Player::Update();
		});

		HotkeyManager::Register("Player.GodModeKey", "GODMODE_KEY", ImGuiKey_None, []()
		{
			Player::SetGodMode(!ConfigManager::B("Player.GodMode"));
		});

		HotkeyManager::Register("Player.InfAmmoKey", "INF_AMMO_KEY", ImGuiKey_None, []()
		{
			Player::SetInfiniteAmmo(!ConfigManager::B("Player.InfAmmo"));
		});
	}
}
