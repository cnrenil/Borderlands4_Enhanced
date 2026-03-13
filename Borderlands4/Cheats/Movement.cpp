#include "pch.h"

namespace
{
    std::recursive_mutex gTeleportMutex;
}

static float LastPinTime = 0.0f;
static SDK::FVector LastPinPos = { 0, 0, 0 };

void PerformMapTeleport()
{
	std::scoped_lock GVarsLock(gGVarsMutex);
	std::scoped_lock TeleLock(gTeleportMutex);
	if (!GVars.PlayerController || !GVars.Character) return;

	SDK::AActor* TargetActor = GVars.Character;
	if (GVars.Character->GetAttachParentActor())
		TargetActor = GVars.Character->GetAttachParentActor();

	SDK::FVector TelePos = LastPinPos;
	SDK::FHitResult HitResult;

	TelePos.Z = 50000.0f;
	TargetActor->K2_SetActorLocation(TelePos, false, &HitResult, false);

	TelePos.Z = -1000.0f;
	TargetActor->K2_SetActorLocation(TelePos, true, &HitResult, false);

	if (ConfigManager::B("Misc.Debug")) {
		SDK::FVector FinalPos = TargetActor->K2_GetActorLocation();
		printf("[MapTP] Teleported to: %.1f, %.1f, %.1f\n", FinalPos.X, FinalPos.Y, FinalPos.Z);
	}

	LastPinPos = { 0, 0, 0 };
}

void DiscoveryPinWatcher()
{
	std::scoped_lock GVarsLock(gGVarsMutex);
	std::scoped_lock TeleLock(gTeleportMutex);
	if (!ConfigManager::B("Misc.MapTeleport") || !GVars.Character || !GVars.Character->PlayerState) return;

	static int LastPinCount = 0;
	try {
		SDK::AOakPlayerState* PS = (SDK::AOakPlayerState*)GVars.Character->PlayerState;
		if (!PS || IsBadReadPtr(PS, sizeof(void*))) return;

		auto& PinArray = PS->DiscoveryPinningState.PinnedDatas;
		int32_t NumPins = PinArray.Num();

		if (NumPins <= 0 || NumPins > 512) {
			LastPinCount = 0;
			return;
		}

		if (NumPins > LastPinCount) {
			for (int i = 0; i < NumPins; i++) {
				if (PinArray[i].pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint) {
					LastPinPos = PinArray[i].PinnedCustomWaypointLocation;
					LastPinTime = (float)ImGui::GetTime();
				}
			}
		} else if (NumPins < LastPinCount) {
			float CurrentTime = (float)ImGui::GetTime();
			if (CurrentTime - LastPinTime < ConfigManager::F("Misc.MapTPWindow") && LastPinPos.X != 0) {
				PerformMapTeleport();
			}
		}
		LastPinCount = NumPins;
	} catch (...) {}
}

void Cheats::SetPlayerSpeed()
{
	if (!GVars.Character) return;
	SDK::AOakCharacter* OakChar = (SDK::AOakCharacter*)GVars.Character;
	SDK::UOakCharacterMovementComponent* OakMove = OakChar->OakCharacterMovement;
	if (!OakMove) return;

	static float originalMaxWalkSpeed = -1.0f;
	static SDK::UOakCharacterMovementComponent* lastMoveComp = nullptr;
	if (lastMoveComp != OakMove) {
		originalMaxWalkSpeed = OakMove->MaxWalkSpeed;
		lastMoveComp = OakMove;
	}

	if (ConfigManager::B("Player.SpeedEnabled")) {
		OakMove->MaxWalkSpeed = originalMaxWalkSpeed * ConfigManager::F("Player.Speed");
	} else if (originalMaxWalkSpeed > 0) {
		OakMove->MaxWalkSpeed = originalMaxWalkSpeed;
	}
}

void Cheats::Flight()
{
	if (!GVars.Character) return;
	SDK::AOakCharacter* OakChar = (SDK::AOakCharacter*)GVars.Character;
	SDK::UOakCharacterMovementComponent* OakMove = OakChar->OakCharacterMovement;
	if (!OakMove) return;

	static float fOldFlySpeed = -1.0f;
	static float fOldWalkSpeed = -1.0f;
	static SDK::UOakCharacterMovementComponent* pLastMoveComp = nullptr;
	static bool bWasFlightOn = false;

	if (pLastMoveComp != OakMove) {
		fOldFlySpeed = -1.0f;
		fOldWalkSpeed = -1.0f;
		bWasFlightOn = false;
		pLastMoveComp = OakMove;
	}

	if (ConfigManager::B("Player.Flight")) {
		if (fOldFlySpeed < 0) fOldFlySpeed = OakMove->MaxFlySpeed;
		if (fOldWalkSpeed < 0) fOldWalkSpeed = OakMove->MaxWalkSpeed;

		const float SpeedMultiplier = std::clamp(ConfigManager::F("Player.FlightSpeed"), 0.1f, 50.0f);
		const float BaseSpeed = (fOldFlySpeed > 1.0f) ? fOldFlySpeed : 600.0f;
		const float TargetSpeed = BaseSpeed * SpeedMultiplier;

		if (OakMove->MovementMode != SDK::EMovementMode::MOVE_Flying)
			OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);

		// Some BL4 movement states still consult walk speed while transitioning.
		// Keep both synchronized so the slider always has effect.
		OakMove->MaxFlySpeed = TargetSpeed;
		OakMove->MaxWalkSpeed = TargetSpeed;

		if (!bWasFlightOn) OakChar->SetActorEnableCollision(false);
		bWasFlightOn = true;
	} else if (bWasFlightOn) {
		OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Walking, 0);
		if (fOldFlySpeed > 0) OakMove->MaxFlySpeed = fOldFlySpeed;
		if (fOldWalkSpeed > 0) OakMove->MaxWalkSpeed = fOldWalkSpeed;
		OakChar->SetActorEnableCollision(true);
		bWasFlightOn = false;
		fOldFlySpeed = -1.0f;
		fOldWalkSpeed = -1.0f;
	}
}

void Cheats::TeleportLoot()
{
	if (!GVars.World || !GVars.Level || !GVars.Character) return;
	
	int32_t NumActors = GVars.Level->Actors.Num();
	if (NumActors < 0 || NumActors > 200000) return;

	SDK::FVector PlayerLoc = GVars.Character->K2_GetActorLocation();
	SDK::FRotator PlayerRot = GVars.Character->K2_GetActorRotation();
	PlayerLoc.Z -= 40.f;

	std::vector<SDK::AInventoryPickup*> Gear;
	for (int i = 0; i < NumActors; i++) {
		SDK::AActor* Actor = GVars.Level->Actors[i];
		if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass())) {
			SDK::AInventoryPickup* Pickup = static_cast<SDK::AInventoryPickup*>(Actor);
			Gear.push_back(Pickup);
		}
	}

	int item_index = 0;
	for (auto g : Gear) {
		float angle = (2.0f * 3.1415926535f / 35.0f) * (float)(item_index % 35);
		float radius = 200.0f + (float)(item_index / 35) * 150.0f;
		SDK::FVector NewLoc = { PlayerLoc.X + cosf(angle) * radius, PlayerLoc.Y + sinf(angle) * radius, PlayerLoc.Z };
		g->K2_SetActorLocation(NewLoc, false, nullptr, false);
		item_index++;
	}
}

void Cheats::UpdateMovement()
{
    Cheats::SetPlayerSpeed();
    Cheats::Flight();
    DiscoveryPinWatcher();
}

bool Cheats::HandleMovementEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	std::scoped_lock GVarsLock(gGVarsMutex);
	std::scoped_lock TeleLock(gTeleportMutex);
    if (!ConfigManager::B("Misc.MapTeleport")) return false;
    const std::string FuncName = Function->GetName();

    if (FuncName == "Server_CreateDiscoveryPin" || FuncName == "Server_AddDiscoveryPin") {
        struct InPinParams { SDK::FGbxDiscoveryPinningPinData InPinData; }*p = (InPinParams*)Params;
        if (p && p->InPinData.pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint) {
            LastPinTime = (float)ImGui::GetTime();
            LastPinPos = p->InPinData.PinnedCustomWaypointLocation;
        }
    } else if (FuncName == "ServerCreatePing") {
        struct InPingParams { SDK::AActor* TargetedActor; SDK::FVector Location; }*p = (InPingParams*)Params;
        if (p) {
            LastPinTime = (float)ImGui::GetTime();
            LastPinPos = p->Location;
        }
    } else if (FuncName == "Server_RemoveDiscoveryPin" || FuncName == "Server_ClearDiscoveryPin" || FuncName == "ServerCancelPing") {
        if (ImGui::GetTime() - LastPinTime < ConfigManager::F("Misc.MapTPWindow") && LastPinPos.X != 0) {
            PerformMapTeleport();
        }
    }
    return false;
}
