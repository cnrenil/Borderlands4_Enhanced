#include "pch.h"
#include "WorldFeature.h"

namespace Features::World
{
	namespace
	{
		std::recursive_mutex gTeleportMutex;
		static SDK::FVector LastPinPos = { 0, 0, 0 };
		static float LastPinTime = 0.0f;
		static int LastPinCount = 0;
	}

	void KillEnemies() {
		if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Kill);
	}

	void SpawnItems() {
		if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Items);
	}

	void SetPlayersOnly(bool enabled) {
		SDK::UWorld* World = Utils::GetWorldSafe();
		if (World && World->GameState && World->GameState->IsA(SDK::AGbxGameState::StaticClass())) {
			static_cast<SDK::AGbxGameState*>(World->GameState)->bRepPlayersOnly = enabled;
		}
	}

	void ClearGroundItems()
	{
		if (!GVars.World || !GVars.Level) return;
		SDK::FVector farLoc = { 100000.f, 100000.f, -100000.f };

		Utils::ForEachLevelActor(GVars.Level, [&](SDK::AActor* Actor)
		{
			if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass()))
				Actor->K2_SetActorLocation(farLoc, false, nullptr, false);
			return true;
		});
	}

	void SetGameSpeed(float Speed)
	{
		SDK::UWorld* World = Utils::GetWorldSafe();
		if (World && World->PersistentLevel && World->PersistentLevel->WorldSettings) {
			World->PersistentLevel->WorldSettings->TimeDilation = Speed;
		}
	}

	void TeleportLoot()
	{
		if (!GVars.Character || !GVars.Level) return;
		SDK::FVector PlayerLoc = GVars.Character->K2_GetActorLocation();
		PlayerLoc.Z -= 40.f;

		std::vector<SDK::AInventoryPickup*> Gear;
		Utils::ForEachLevelActor(GVars.Level, [&](SDK::AActor* Actor)
		{
			if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass()))
				Gear.push_back(static_cast<SDK::AInventoryPickup*>(Actor));
			return true;
		});

		int item_index = 0;
		for (auto g : Gear) {
			float angle = (2.0f * 3.1415926535f / 35.0f) * (float)(item_index % 35);
			float radius = 200.0f + (float)(item_index / 35) * 150.0f;
			SDK::FVector NewLoc = { PlayerLoc.X + cosf(angle) * radius, PlayerLoc.Y + sinf(angle) * radius, PlayerLoc.Z };
			g->K2_SetActorLocation(NewLoc, false, nullptr, false);
			item_index++;
		}
	}

	void PerformMapTeleport()
	{
		std::scoped_lock TeleLock(gTeleportMutex);

		if (LastPinPos.X == 0 && LastPinPos.Y == 0 && LastPinPos.Z == 0)
		{
			LOG_DEBUG("World", "[Teleport] LastPinPos is Zero. Ignoring PerformMapTeleport.");
			return;
		}

		auto PC = GVars.PlayerController;
		if (!PC || !PC->Pawn)
		{
			LOG_ERROR("World", "[Teleport] Missing PlayerController or Pawn.");
			return;
		}

		SDK::AActor* TargetActor = PC->Pawn;
		if (TargetActor->GetAttachParentActor())
			TargetActor = TargetActor->GetAttachParentActor();

		SDK::FVector TelePos = LastPinPos;
		SDK::FHitResult HitResult;

		LOG_INFO("World", "[Teleport] Attempting teleport to (%.2f, %.2f, %.2f)", TelePos.X, TelePos.Y, TelePos.Z);

		// Move way above the map and then sweep down to the ground
		TelePos.Z = 50000.0f;
		TargetActor->K2_SetActorLocation(TelePos, false, &HitResult, false);

		TelePos.Z = -1000.0f;
		TargetActor->K2_SetActorLocation(TelePos, true, &HitResult, false);
		
		LOG_INFO("World", "[Teleport] Completed. New Location: (%.2f, %.2f, %.2f)", 
			TargetActor->K2_GetActorLocation().X, TargetActor->K2_GetActorLocation().Y, TargetActor->K2_GetActorLocation().Z);

		LastPinPos = { 0, 0, 0 };
		LastPinTime = 0.0f;
	}

	bool OnEvent(const Core::SchedulerGameEvent& Event)
	{
		if (!ConfigManager::B("Misc.MapTeleport")) return false;

		std::string FuncName = Event.Function->GetName();

		if (FuncName == "Server_CreateDiscoveryPin") {
			auto* p = static_cast<SDK::Params::OakPlayerState_Server_CreateDiscoveryPin*>(Event.Params);
			if (p && p->InPinData.pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint) {
				std::scoped_lock TeleLock(gTeleportMutex);
				LastPinPos = p->InPinData.PinnedCustomWaypointLocation;
				LastPinTime = (float)ImGui::GetTime();
				LOG_DEBUG("World", "[Event] Discovery Pin captured at (%.2f, %.2f, %.2f)", LastPinPos.X, LastPinPos.Y, LastPinPos.Z);
			}
		}
		else if (FuncName == "ServerCreatePing") {
			auto* p = static_cast<SDK::Params::OakPlayerController_ServerCreatePing*>(Event.Params);
			if (p) {
				std::scoped_lock TeleLock(gTeleportMutex);
				LastPinPos = p->Location;
				LastPinTime = (float)ImGui::GetTime();
				LOG_DEBUG("World", "[Event] Ping captured at (%.2f, %.2f, %.2f)", LastPinPos.X, LastPinPos.Y, LastPinPos.Z);
			}
		}
		else if (FuncName == "Server_RemoveDiscoveryPin" || FuncName == "ServerCancelPing") {
			float CurrentTime = (float)ImGui::GetTime();
			float Delta = CurrentTime - LastPinTime;
			std::scoped_lock TeleLock(gTeleportMutex);
			
			if (Delta < ConfigManager::F("Misc.MapTPWindow") && LastPinPos.X != 0) {
				LOG_DEBUG("World", "[Event] Teleport triggered by '%s' (delta: %.2fs)", FuncName.c_str(), Delta);
				PerformMapTeleport();
			}
			else {
				LOG_DEBUG("World", "[Event] Remove '%s' ignored (delta: %.2fs, posValid: %d)", FuncName.c_str(), Delta, (LastPinPos.X != 0));
			}
		}

		return false;
	}

	void DiscoveryPinWatcher()
	{
		if (!ConfigManager::B("Misc.MapTeleport") || !GVars.Character || !GVars.Character->PlayerState) return;

		try {
			SDK::AOakPlayerState* PS = (SDK::AOakPlayerState*)GVars.Character->PlayerState;
			if (!PS || IsBadReadPtr(PS, sizeof(void*))) return;

			auto& PinArray = PS->DiscoveryPinningState.PinnedDatas;
			int32_t NumPins = PinArray.Num();

			if (NumPins < 0 || NumPins > 512) {
				LastPinCount = 0;
				return;
			}

			if (NumPins > LastPinCount) {
				for (int i = 0; i < NumPins; i++) {
					if (PinArray[i].pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint) {
						std::scoped_lock TeleLock(gTeleportMutex);
						LastPinPos = PinArray[i].PinnedCustomWaypointLocation;
						LastPinTime = (float)ImGui::GetTime();
					}
				}
			}
			else if (NumPins < LastPinCount) {
				float CurrentTime = (float)ImGui::GetTime();
				std::scoped_lock TeleLock(gTeleportMutex);
				if (CurrentTime - LastPinTime < ConfigManager::F("Misc.MapTPWindow") && LastPinPos.X != 0) {
					PerformMapTeleport();
				}
			}
			LastPinCount = NumPins;
		}
		catch (...) {}
	}

	void Update()
	{
		DiscoveryPinWatcher();
	}
}

namespace Features
{
	void RegisterWorldFeature()
	{
		Core::Scheduler::RegisterGameplayTickCallback("World", []()
		{
			World::Update();
		});

		Core::Scheduler::RegisterEventHandler("World", [](const Core::SchedulerGameEvent& Event)
		{
			return World::OnEvent(Event);
		});

		HotkeyManager::Register("Misc.TeleportLootKey", "TELEPORT_LOOT_KEY", ImGuiKey_F6, []()
		{
			World::TeleportLoot();
		});
	}
}
