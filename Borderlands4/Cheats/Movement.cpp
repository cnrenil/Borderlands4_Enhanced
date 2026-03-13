#include "pch.h"

namespace
{
    std::recursive_mutex gTeleportMutex;

    struct MovementOriginalState
    {
        float MaxWalkSpeed = -1.0f;
        float MaxWalkSpeedCrouched = -1.0f;
        float MaxSwimSpeed = -1.0f;
        float MaxCustomMovementSpeed = -1.0f;
        float MaxFlySpeed = -1.0f;
        float MaxAcceleration = -1.0f;
        float BrakingDecelerationWalking = -1.0f;
        float BrakingDecelerationFlying = -1.0f;
        float MaxGroundSpeedScaleValue = -1.0f;
        float MaxGroundSpeedScaleBase = -1.0f;
    };

    static SDK::UOakCharacterMovementComponent* g_LastMoveComp = nullptr;
    static SDK::UOakCharacterMovementComponent* g_LastGlideMoveComp = nullptr;
    static MovementOriginalState g_OriginalMoveState{};
    static bool g_WasSpeedHackOn = false;
    static bool g_WasFlightOn = false;
    static bool g_WasInfGlideStaminaOn = false;
    static float g_LastSpeedMultiplier = 1.0f;
    static float g_LastFlightMultiplier = 1.0f;
    static float g_OriginalGlideCostValue = -1.0f;
    static float g_OriginalGlideCostBase = -1.0f;
    static double g_LastGlideRefreshTime = 0.0;

    void ResetMovementState()
    {
        g_OriginalMoveState = MovementOriginalState{};
        g_WasSpeedHackOn = false;
        g_WasFlightOn = false;
        g_WasInfGlideStaminaOn = false;
        g_LastSpeedMultiplier = 1.0f;
        g_LastFlightMultiplier = 1.0f;
        g_OriginalGlideCostValue = -1.0f;
        g_OriginalGlideCostBase = -1.0f;
        g_LastGlideRefreshTime = 0.0;
    }

    void CaptureMovementOriginals(SDK::UOakCharacterMovementComponent* Move)
    {
        if (!Move) return;

        if (g_OriginalMoveState.MaxWalkSpeed <= 1.0f && Move->MaxWalkSpeed > 1.0f)
            g_OriginalMoveState.MaxWalkSpeed = Move->MaxWalkSpeed;
        if (g_OriginalMoveState.MaxWalkSpeedCrouched <= 1.0f && Move->MaxWalkSpeedCrouched > 1.0f)
            g_OriginalMoveState.MaxWalkSpeedCrouched = Move->MaxWalkSpeedCrouched;
        if (g_OriginalMoveState.MaxSwimSpeed <= 1.0f && Move->MaxSwimSpeed > 1.0f)
            g_OriginalMoveState.MaxSwimSpeed = Move->MaxSwimSpeed;
        if (g_OriginalMoveState.MaxCustomMovementSpeed <= 1.0f && Move->MaxCustomMovementSpeed > 1.0f)
            g_OriginalMoveState.MaxCustomMovementSpeed = Move->MaxCustomMovementSpeed;
        if (g_OriginalMoveState.MaxFlySpeed <= 1.0f && Move->MaxFlySpeed > 1.0f)
            g_OriginalMoveState.MaxFlySpeed = Move->MaxFlySpeed;
        if (g_OriginalMoveState.MaxAcceleration <= 1.0f && Move->MaxAcceleration > 1.0f)
            g_OriginalMoveState.MaxAcceleration = Move->MaxAcceleration;
        if (g_OriginalMoveState.BrakingDecelerationWalking <= 1.0f && Move->BrakingDecelerationWalking > 1.0f)
            g_OriginalMoveState.BrakingDecelerationWalking = Move->BrakingDecelerationWalking;
        if (g_OriginalMoveState.BrakingDecelerationFlying <= 1.0f && Move->BrakingDecelerationFlying > 1.0f)
            g_OriginalMoveState.BrakingDecelerationFlying = Move->BrakingDecelerationFlying;

        if (g_OriginalMoveState.MaxGroundSpeedScaleValue <= 0.01f && Move->MaxGroundSpeedScale.Value > 0.01f)
            g_OriginalMoveState.MaxGroundSpeedScaleValue = Move->MaxGroundSpeedScale.Value;
        if (g_OriginalMoveState.MaxGroundSpeedScaleBase <= 0.01f && Move->MaxGroundSpeedScale.BaseValue > 0.01f)
            g_OriginalMoveState.MaxGroundSpeedScaleBase = Move->MaxGroundSpeedScale.BaseValue;
    }

    float ResolveBase(float Original, float Current, float Fallback)
    {
        if (Original > 0.01f) return Original;
        if (Current > 0.01f) return Current;
        return Fallback;
    }

    void ScaleVelocity(SDK::UOakCharacterMovementComponent* Move, float Multiplier, bool bScaleZ)
    {
        if (!Move) return;
        if (fabsf(Multiplier - 1.0f) < 0.001f) return;

        SDK::FVector Vel = Move->Velocity;
        const float speedSq = Vel.X * Vel.X + Vel.Y * Vel.Y + Vel.Z * Vel.Z;
        if (speedSq < 1.0f) return;

        Vel.X *= Multiplier;
        Vel.Y *= Multiplier;
        if (bScaleZ) Vel.Z *= Multiplier;
        Move->Velocity = Vel;
    }

    SDK::AOakVehicle* GetControlledVehicle()
    {
        if (GVars.PlayerController && Utils::IsValidActor(GVars.PlayerController))
        {
            SDK::APawn* controlledPawn = GVars.PlayerController->Pawn;
            if (controlledPawn && Utils::IsValidActor(controlledPawn) && controlledPawn->IsA(SDK::AOakVehicle::StaticClass()))
                return reinterpret_cast<SDK::AOakVehicle*>(controlledPawn);
        }

        SDK::AActor* selfActor = Utils::GetSelfActor();
        if (selfActor && Utils::IsValidActor(selfActor))
        {
            if (selfActor->IsA(SDK::AOakVehicle::StaticClass()))
                return reinterpret_cast<SDK::AOakVehicle*>(selfActor);

            SDK::AActor* attachedParent = selfActor->GetAttachParentActor();
            if (attachedParent && Utils::IsValidActor(attachedParent) && attachedParent->IsA(SDK::AOakVehicle::StaticClass()))
                return reinterpret_cast<SDK::AOakVehicle*>(attachedParent);
        }

        return nullptr;
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
        }

        if (g_OriginalGlideCostValue < 0.0f) g_OriginalGlideCostValue = move->VaultPowerCost_Glide.Value;
        if (g_OriginalGlideCostBase < 0.0f) g_OriginalGlideCostBase = move->VaultPowerCost_Glide.BaseValue;

        move->VaultPowerCost_Glide.Value = 0.0f;
        move->VaultPowerCost_Glide.BaseValue = 0.0f;
        g_WasInfGlideStaminaOn = true;

        const double now = ImGui::GetTime();
        if ((now - g_LastGlideRefreshTime) > 0.2)
        {
            move->ClientOnVaultPowerNotDepleted();
            g_LastGlideRefreshTime = now;
        }
    }

    void ApplyInfiniteVehicleBoost()
    {
        if (!ConfigManager::B("Player.InfVehicleBoost"))
        {
            return;
        }

        SDK::AOakVehicle* vehicle = GetControlledVehicle();
        if (!vehicle || !Utils::IsValidActor(vehicle)) return;

        SDK::UOakWheeledVehicleMovementComponent* vehicleMove = vehicle->OakVehicleMovement;
        if (!vehicleMove) return;

        // Keep boost state out of "depleted/cooldown" so boost can be reused immediately.
        vehicleMove->LastBoostFailureReason = SDK::EBoostFailureReason::None;
        vehicleMove->OnBoostFilled();

        // Ask server-side vehicle logic to recompute boost state if gameplay code marked it invalid.
        if (!SDK::UOakVehicleBlueprintLibrary::CanBoost(vehicle))
            vehicle->ServerRecomputeBoostCost();

    }
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

	if (g_LastMoveComp != OakMove) {
		g_LastMoveComp = OakMove;
		ResetMovementState();
	}
	CaptureMovementOriginals(OakMove);

	if (ConfigManager::B("Player.SpeedEnabled")) {
		const float speedMultiplier = std::clamp(ConfigManager::F("Player.Speed"), 0.1f, 20.0f);
		const float baseWalk = ResolveBase(g_OriginalMoveState.MaxWalkSpeed, OakMove->MaxWalkSpeed, 600.0f);
		const float baseWalkCrouched = ResolveBase(g_OriginalMoveState.MaxWalkSpeedCrouched, OakMove->MaxWalkSpeedCrouched, baseWalk * 0.5f);
		const float baseSwim = ResolveBase(g_OriginalMoveState.MaxSwimSpeed, OakMove->MaxSwimSpeed, baseWalk);
		const float baseCustom = ResolveBase(g_OriginalMoveState.MaxCustomMovementSpeed, OakMove->MaxCustomMovementSpeed, baseWalk);
		const float baseAccel = ResolveBase(g_OriginalMoveState.MaxAcceleration, OakMove->MaxAcceleration, 2048.0f);
		const float baseBrakeWalk = ResolveBase(g_OriginalMoveState.BrakingDecelerationWalking, OakMove->BrakingDecelerationWalking, 2048.0f);
		const float baseGroundScale = ResolveBase(g_OriginalMoveState.MaxGroundSpeedScaleBase, OakMove->MaxGroundSpeedScale.BaseValue, 1.0f);

		OakMove->MaxWalkSpeed = baseWalk * speedMultiplier;
		OakMove->MaxWalkSpeedCrouched = baseWalkCrouched * speedMultiplier;
		OakMove->MaxSwimSpeed = baseSwim * speedMultiplier;
		OakMove->MaxCustomMovementSpeed = baseCustom * speedMultiplier;
		OakMove->MaxAcceleration = baseAccel * speedMultiplier;
		OakMove->BrakingDecelerationWalking = baseBrakeWalk * speedMultiplier;
		OakMove->MaxGroundSpeedScale.Value = baseGroundScale * speedMultiplier;
		OakMove->MaxGroundSpeedScale.BaseValue = baseGroundScale * speedMultiplier;

		// Some movement states ignore speed caps but still use current velocity.
		const float velocityRatio = speedMultiplier / (std::max)(g_LastSpeedMultiplier, 0.01f);
		ScaleVelocity(OakMove, velocityRatio, false);
		g_LastSpeedMultiplier = speedMultiplier;
		g_WasSpeedHackOn = true;
	}
	else if (g_WasSpeedHackOn) {
		if (g_OriginalMoveState.MaxWalkSpeed > 0.01f) OakMove->MaxWalkSpeed = g_OriginalMoveState.MaxWalkSpeed;
		if (g_OriginalMoveState.MaxWalkSpeedCrouched > 0.01f) OakMove->MaxWalkSpeedCrouched = g_OriginalMoveState.MaxWalkSpeedCrouched;
		if (g_OriginalMoveState.MaxSwimSpeed > 0.01f) OakMove->MaxSwimSpeed = g_OriginalMoveState.MaxSwimSpeed;
		if (g_OriginalMoveState.MaxCustomMovementSpeed > 0.01f) OakMove->MaxCustomMovementSpeed = g_OriginalMoveState.MaxCustomMovementSpeed;
		if (g_OriginalMoveState.MaxAcceleration > 0.01f) OakMove->MaxAcceleration = g_OriginalMoveState.MaxAcceleration;
		if (g_OriginalMoveState.BrakingDecelerationWalking > 0.01f) OakMove->BrakingDecelerationWalking = g_OriginalMoveState.BrakingDecelerationWalking;
		if (g_OriginalMoveState.MaxGroundSpeedScaleValue > 0.01f) OakMove->MaxGroundSpeedScale.Value = g_OriginalMoveState.MaxGroundSpeedScaleValue;
		if (g_OriginalMoveState.MaxGroundSpeedScaleBase > 0.01f) OakMove->MaxGroundSpeedScale.BaseValue = g_OriginalMoveState.MaxGroundSpeedScaleBase;
		g_LastSpeedMultiplier = 1.0f;
		g_WasSpeedHackOn = false;
	}
}

void Cheats::Flight()
{
	if (!GVars.Character) return;
	SDK::AOakCharacter* OakChar = (SDK::AOakCharacter*)GVars.Character;
	SDK::UOakCharacterMovementComponent* OakMove = OakChar->OakCharacterMovement;
	if (!OakMove) return;

	if (g_LastMoveComp != OakMove) {
		g_LastMoveComp = OakMove;
		ResetMovementState();
	}
	CaptureMovementOriginals(OakMove);

	if (ConfigManager::B("Player.Flight")) {
		const float SpeedMultiplier = std::clamp(ConfigManager::F("Player.FlightSpeed"), 0.1f, 50.0f);
		const float BaseSpeed = ResolveBase(g_OriginalMoveState.MaxFlySpeed, OakMove->MaxFlySpeed, 600.0f);
		const float TargetSpeed = BaseSpeed * SpeedMultiplier;

		if (OakMove->MovementMode != SDK::EMovementMode::MOVE_Flying)
			OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);

		// Some BL4 movement states still consult walk speed while transitioning.
		// Keep both synchronized so the slider always has effect.
		OakMove->MaxFlySpeed = TargetSpeed;
		OakMove->MaxWalkSpeed = TargetSpeed;
		OakMove->MaxAcceleration = ResolveBase(g_OriginalMoveState.MaxAcceleration, OakMove->MaxAcceleration, 2048.0f) * SpeedMultiplier;
		OakMove->BrakingDecelerationFlying = ResolveBase(g_OriginalMoveState.BrakingDecelerationFlying, OakMove->BrakingDecelerationFlying, 2048.0f) * SpeedMultiplier;

		// If game-side logic overwrites max speed, velocity scaling still guarantees visible speed change.
		const float velocityRatio = SpeedMultiplier / (std::max)(g_LastFlightMultiplier, 0.01f);
		ScaleVelocity(OakMove, velocityRatio, true);
		g_LastFlightMultiplier = SpeedMultiplier;

		if (!g_WasFlightOn) OakChar->SetActorEnableCollision(false);
		g_WasFlightOn = true;
	}
	else if (g_WasFlightOn) {
		OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Walking, 0);
		if (g_OriginalMoveState.MaxFlySpeed > 0.01f) OakMove->MaxFlySpeed = g_OriginalMoveState.MaxFlySpeed;
		if (g_OriginalMoveState.MaxWalkSpeed > 0.01f) OakMove->MaxWalkSpeed = g_OriginalMoveState.MaxWalkSpeed;
		if (g_OriginalMoveState.MaxAcceleration > 0.01f) OakMove->MaxAcceleration = g_OriginalMoveState.MaxAcceleration;
		if (g_OriginalMoveState.BrakingDecelerationFlying > 0.01f) OakMove->BrakingDecelerationFlying = g_OriginalMoveState.BrakingDecelerationFlying;
		OakChar->SetActorEnableCollision(true);
		g_LastFlightMultiplier = 1.0f;
		g_WasFlightOn = false;
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
    ApplyInfiniteGlideStamina();
    ApplyInfiniteVehicleBoost();
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
