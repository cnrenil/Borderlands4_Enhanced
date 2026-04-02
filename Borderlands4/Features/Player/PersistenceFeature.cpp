#include "pch.h"
#include "Features/Player/PlayerFeature.h"
#include "Features/World/WorldFeature.h"

namespace Features
{
	namespace
	{
		void EnforcePersistenceFeature()
		{
			static bool bOneShotAppliedAfterInjection = false;

			if (!GVars.PlayerController || !GVars.Character)
			{
				return;
			}

			GVars.Character->bCanBeDamaged = !ConfigManager::B("Player.GodMode");
			Features::World::SetGameSpeed(ConfigManager::F("Player.GameSpeed"));
			Features::World::SetPlayersOnly(ConfigManager::B("Player.PlayersOnly"));

			if (!bOneShotAppliedAfterInjection)
			{
				bOneShotAppliedAfterInjection = true;
				using namespace ConfigManager;

				if (B("Player.Demigod")) Features::Player::SetDemigod(true);
				if (B("Player.InfAmmo")) Features::Player::SetInfiniteAmmo(true);
				if (B("Player.NoTarget")) Features::Player::SetNoTarget(true);
			}
		}
	}

	void RegisterPersistenceFeature()
	{
		Core::Scheduler::RegisterGameplayTickCallback("Persistence", []()
		{
			EnforcePersistenceFeature();
		});
	}
}
