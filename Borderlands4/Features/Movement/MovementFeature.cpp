#include "pch.h"
#include "MovementFeature.h"
#include "Features/World/WorldFeature.h"

namespace Features::Movement
{
    namespace
    {

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
            float CustomTimeDilationValue = -1.0f;
            float CustomTimeDilationBase = -1.0f;
        };

        struct VehicleOriginalState
        {
            bool Captured = false;
            float MaxSpeedValue = -1.0f;
            float MaxSpeedBase = -1.0f;
            float BoostMaxSpeedValue = -1.0f;
            float BoostMaxSpeedBase = -1.0f;
            float MaxAccelValue = -1.0f;
            float MaxAccelBase = -1.0f;
            float BoostMaxAccelValue = -1.0f;
            float BoostMaxAccelBase = -1.0f;
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
        static float g_OriginalDashCostValue = -1.0f;
        static float g_OriginalDashCostBase = -1.0f;
        static double g_LastGlideRefreshTime = 0.0;
        static SDK::FVehicleAttributesState* g_LastVehicleAttrState = nullptr;
        static VehicleOriginalState g_OriginalVehicleState{};
        static bool g_WasVehicleSpeedHackOn = false;

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
            g_OriginalDashCostValue = -1.0f;
            g_OriginalDashCostBase = -1.0f;
            g_LastGlideRefreshTime = 0.0;
            g_LastVehicleAttrState = nullptr;
            g_OriginalVehicleState = VehicleOriginalState{};
            g_WasVehicleSpeedHackOn = false;
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

            SDK::AActor* owner = Move->GetOwner();
            if (owner)
            {
                if (g_OriginalMoveState.CustomTimeDilationValue <= 0.01f && owner->CustomTimeDilation.Value > 0.01f)
                    g_OriginalMoveState.CustomTimeDilationValue = owner->CustomTimeDilation.Value;
                if (g_OriginalMoveState.CustomTimeDilationBase <= 0.01f && owner->CustomTimeDilation.BaseValue > 0.01f)
                    g_OriginalMoveState.CustomTimeDilationBase = owner->CustomTimeDilation.BaseValue;
            }
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

        SDK::FVehicleAttributesState* GetVehicleAttributesState()
        {
            SDK::AOakVehicle* localVehicle = GetControlledVehicle();
            if (!localVehicle) return nullptr;

            auto* localChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
            if (localChar && Utils::IsValidActor(localChar) && localChar->IsA(SDK::AOakCharacter::StaticClass()))
            {
                SDK::UVehicleDriverComponent* driverComp = localChar->VehicleDriverComponent.Get();
                if (driverComp)
                {
                    SDK::AOakVehicle* drivenVehicle = driverComp->VehicleDriverState.DrivenVehicle;
                    if (!drivenVehicle || drivenVehicle == localVehicle)
                        return &driverComp->VehicleAttributesState;
                }
            }
            return nullptr;
        }

        void ApplyVehicleSpeedMultiplier()
        {
            const bool enabled = ConfigManager::B("Player.VehicleSpeedEnabled");
            SDK::FVehicleAttributesState* attrState = GetVehicleAttributesState();

            if (!enabled)
            {
                if (g_WasVehicleSpeedHackOn && g_OriginalVehicleState.Captured && attrState)
                {
                    if (g_OriginalVehicleState.MaxSpeedValue > 0.01f) attrState->maxspeed.Value = g_OriginalVehicleState.MaxSpeedValue;
                    if (g_OriginalVehicleState.MaxSpeedBase > 0.01f) attrState->maxspeed.BaseValue = g_OriginalVehicleState.MaxSpeedBase;
                    if (g_OriginalVehicleState.BoostMaxSpeedValue > 0.01f) attrState->BoostMaxSpeed.Value = g_OriginalVehicleState.BoostMaxSpeedValue;
                    if (g_OriginalVehicleState.BoostMaxSpeedBase > 0.01f) attrState->BoostMaxSpeed.BaseValue = g_OriginalVehicleState.BoostMaxSpeedBase;
                    if (g_OriginalVehicleState.MaxAccelValue > 0.01f) attrState->MaxAccel.Value = g_OriginalVehicleState.MaxAccelValue;
                    if (g_OriginalVehicleState.MaxAccelBase > 0.01f) attrState->MaxAccel.BaseValue = g_OriginalVehicleState.MaxAccelBase;
                    if (g_OriginalVehicleState.BoostMaxAccelValue > 0.01f) attrState->BoostMaxAccel.Value = g_OriginalVehicleState.BoostMaxAccelValue;
                    if (g_OriginalVehicleState.BoostMaxAccelBase > 0.01f) attrState->BoostMaxAccel.BaseValue = g_OriginalVehicleState.BoostMaxAccelBase;

                    g_WasVehicleSpeedHackOn = false;
                    g_LastVehicleAttrState = nullptr;
                    g_OriginalVehicleState = VehicleOriginalState{};
                }
                return;
            }

            if (!attrState) return;

            if (g_LastVehicleAttrState != attrState)
            {
                g_LastVehicleAttrState = attrState;
                g_OriginalVehicleState = VehicleOriginalState{};
            }

            if (!g_OriginalVehicleState.Captured)
            {
                g_OriginalVehicleState.MaxSpeedValue = attrState->maxspeed.Value;
                g_OriginalVehicleState.MaxSpeedBase = attrState->maxspeed.BaseValue;
                g_OriginalVehicleState.BoostMaxSpeedValue = attrState->BoostMaxSpeed.Value;
                g_OriginalVehicleState.BoostMaxSpeedBase = attrState->BoostMaxSpeed.BaseValue;
                g_OriginalVehicleState.MaxAccelValue = attrState->MaxAccel.Value;
                g_OriginalVehicleState.MaxAccelBase = attrState->MaxAccel.BaseValue;
                g_OriginalVehicleState.BoostMaxAccelValue = attrState->BoostMaxAccel.Value;
                g_OriginalVehicleState.BoostMaxAccelBase = attrState->BoostMaxAccel.BaseValue;
                g_OriginalVehicleState.Captured = true;
            }

        const float speedMultiplier = (std::max)(ConfigManager::F("Player.VehicleSpeed"), 0.1f);
            const float baseMaxSpeed = ResolveBase(g_OriginalVehicleState.MaxSpeedBase, attrState->maxspeed.BaseValue, 1.0f);
            const float baseBoostMaxSpeed = ResolveBase(g_OriginalVehicleState.BoostMaxSpeedBase, attrState->BoostMaxSpeed.BaseValue, baseMaxSpeed);
            const float baseMaxAccel = ResolveBase(g_OriginalVehicleState.MaxAccelBase, attrState->MaxAccel.BaseValue, 1.0f);
            const float baseBoostMaxAccel = ResolveBase(g_OriginalVehicleState.BoostMaxAccelBase, attrState->BoostMaxAccel.BaseValue, baseMaxAccel);

            attrState->maxspeed.Value = baseMaxSpeed * speedMultiplier;
            attrState->maxspeed.BaseValue = baseMaxSpeed * speedMultiplier;
            attrState->BoostMaxSpeed.Value = baseBoostMaxSpeed * speedMultiplier;
            attrState->BoostMaxSpeed.BaseValue = baseBoostMaxSpeed * speedMultiplier;
            attrState->MaxAccel.Value = baseMaxAccel * speedMultiplier;
            attrState->MaxAccel.BaseValue = baseMaxAccel * speedMultiplier;
            attrState->BoostMaxAccel.Value = baseBoostMaxAccel * speedMultiplier;
            attrState->BoostMaxAccel.BaseValue = baseBoostMaxAccel * speedMultiplier;
            g_WasVehicleSpeedHackOn = true;
        }
	}

    void SetPlayerSpeed(float Speed)
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
            const float speedMultiplier = std::clamp(Speed, 0.1f, 20.0f);
            const float baseGroundScale = ResolveBase(g_OriginalMoveState.MaxGroundSpeedScaleBase, OakMove->MaxGroundSpeedScale.BaseValue, 1.0f);
            const float baseTimeDilation = ResolveBase(g_OriginalMoveState.CustomTimeDilationBase, OakChar->CustomTimeDilation.BaseValue, 1.0f);

            OakMove->MaxGroundSpeedScale.Value = baseGroundScale * speedMultiplier;
            OakMove->MaxGroundSpeedScale.BaseValue = baseGroundScale * speedMultiplier;
            OakChar->CustomTimeDilation.Value = baseTimeDilation * speedMultiplier;
            OakChar->CustomTimeDilation.BaseValue = baseTimeDilation * speedMultiplier;

            const float velocityRatio = speedMultiplier / (std::max)(g_LastSpeedMultiplier, 0.01f);
            ScaleVelocity(OakMove, velocityRatio, false);
            g_LastSpeedMultiplier = speedMultiplier;
            g_WasSpeedHackOn = true;
        }
        else if (g_WasSpeedHackOn) {
            if (g_OriginalMoveState.MaxGroundSpeedScaleValue > 0.01f) OakMove->MaxGroundSpeedScale.Value = g_OriginalMoveState.MaxGroundSpeedScaleValue;
            if (g_OriginalMoveState.MaxGroundSpeedScaleBase > 0.01f) OakMove->MaxGroundSpeedScale.BaseValue = g_OriginalMoveState.MaxGroundSpeedScaleBase;
            if (g_OriginalMoveState.CustomTimeDilationValue > 0.01f) OakChar->CustomTimeDilation.Value = g_OriginalMoveState.CustomTimeDilationValue;
            if (g_OriginalMoveState.CustomTimeDilationBase > 0.01f) OakChar->CustomTimeDilation.BaseValue = g_OriginalMoveState.CustomTimeDilationBase;
            g_LastSpeedMultiplier = 1.0f;
            g_WasSpeedHackOn = false;
        }
    }

    void SetFlight(bool enabled, float Speed)
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

        if (enabled) {
            const float SpeedMultiplier = std::clamp(Speed, 0.1f, 50.0f);
            const float BaseSpeed = ResolveBase(g_OriginalMoveState.MaxFlySpeed, OakMove->MaxFlySpeed, 600.0f);
            const float TargetSpeed = BaseSpeed * SpeedMultiplier;

            if (OakMove->MovementMode != SDK::EMovementMode::MOVE_Flying)
                OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);

            OakMove->MaxFlySpeed = TargetSpeed;
            OakMove->MaxWalkSpeed = TargetSpeed;
            OakMove->MaxAcceleration = ResolveBase(g_OriginalMoveState.MaxAcceleration, OakMove->MaxAcceleration, 2048.0f) * SpeedMultiplier;
            OakMove->BrakingDecelerationFlying = ResolveBase(g_OriginalMoveState.BrakingDecelerationFlying, OakMove->BrakingDecelerationFlying, 2048.0f) * SpeedMultiplier;

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

    void Update()
    {
        SetPlayerSpeed(ConfigManager::F("Player.Speed"));
        SetFlight(ConfigManager::B("Player.Flight"), ConfigManager::F("Player.FlightSpeed"));
        ApplyVehicleSpeedMultiplier();
    }
}

namespace Features
{
    void RegisterMovementFeature()
    {
        Core::Scheduler::RegisterGameTickCallback("Movement", []()
        {
            Movement::Update();
        });
    }
}
