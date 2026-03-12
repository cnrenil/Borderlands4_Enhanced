#include "pch.h"

using namespace SDK;


std::vector<bool> CheatToggles;

ImVec2 CheatOptionsWindowSize = ImVec2(0, 0);

void Cheats::SetPlayerSpeed()
{
	if (!GVars.Character) return;

	AOakCharacter* OakChar = (AOakCharacter*)GVars.Character;
	UOakCharacterMovementComponent* OakMove = OakChar->OakCharacterMovement;
	if (!OakMove) return;

	static float lastSpeedValue = 1.0f;
	static float originalMaxWalkSpeed = -1.0f;

	// Reset if character changed or component changed
	static UOakCharacterMovementComponent* lastMoveComp = nullptr;
	if (lastMoveComp != OakMove) {
		originalMaxWalkSpeed = OakMove->MaxWalkSpeed;
		lastMoveComp = OakMove;
	}

	if (CVars.SpeedEnabled)
	{
		// Constant enforcement
		OakMove->MaxWalkSpeed = originalMaxWalkSpeed * CVars.Speed;
		lastSpeedValue = CVars.Speed;
	}
	else
	{
		if (originalMaxWalkSpeed > 0)
			OakMove->MaxWalkSpeed = originalMaxWalkSpeed;
	}
}

void Cheats::Flight()
{
	if (!GVars.Character) return;

	AOakCharacter* OakChar = (AOakCharacter*)GVars.Character;
	UOakCharacterMovementComponent* OakMove = OakChar->OakCharacterMovement;
	if (!OakMove) return;

	static float fOldFlySpeed = -1.0f;
	static bool bWasFlightOn = false;

	if (CVars.FlightEnabled)
	{
		if (fOldFlySpeed < 0)
			fOldFlySpeed = OakMove->MaxFlySpeed;

		// Force the mode every frame to prevent game from changing it back
		if (OakMove->MovementMode != SDK::EMovementMode::MOVE_Flying)
			OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Flying, 0);

		OakMove->MaxFlySpeed = 600.0f * (CVars.FlightSpeed * 2.0f); // Use a baseline or multiplier
		
		static bool collisionDisabled = false;
		if (!collisionDisabled) {
			OakChar->SetActorEnableCollision(false);
			collisionDisabled = true;
		}
		
		bWasFlightOn = true;
	}
	else
	{
		if (bWasFlightOn)
		{
			OakMove->SetMovementMode(SDK::EMovementMode::MOVE_Walking, 0);
			if (fOldFlySpeed > 0)
				OakMove->MaxFlySpeed = fOldFlySpeed;
			OakChar->SetActorEnableCollision(true);
			bWasFlightOn = false;
			fOldFlySpeed = -1.0f;
		}
	}
}

void Cheats::ToggleNoTarget()
{
	if (!GVars.PlayerController || !GVars.Character) return;
	SDK::UGbxTargetingFunctionLibrary::LockTargetableByAI(GVars.Character, SDK::UKismetStringLibrary::Conv_StringToName(L"guest"), CVars.NoTarget, CVars.NoTarget);
}

void Cheats::KillEnemies()
{
	if (GVars.PlayerController && GVars.PlayerController->IsA(AOakPlayerController::StaticClass()))
		static_cast<AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Kill);
}

void Cheats::GiveLevels()
{
	if (GVars.PlayerController && GVars.PlayerController->IsA(AOakPlayerController::StaticClass()))
		static_cast<AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Levels);
}

void Cheats::SpawnItems()
{
	if (GVars.PlayerController && GVars.PlayerController->IsA(AOakPlayerController::StaticClass()))
		static_cast<AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Items);
}

void Cheats::ToggleDemigod()
{
	if (GVars.PlayerController && GVars.PlayerController->IsA(AOakPlayerController::StaticClass()))
		static_cast<AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Demigod);
}

void Cheats::TogglePlayersOnly()
{
	if (!GVars.PlayerController) return;
	if (!GVars.PlayerController->CheatManager)
		GVars.PlayerController->CheatManager = (UCheatManager*)SDK::UGameplayStatics::SpawnObject(SDK::UOakCheatManager::StaticClass(), GVars.PlayerController);
	if (GVars.PlayerController->CheatManager)
		GVars.PlayerController->CheatManager->PlayersOnly();
}

void Cheats::SetGameSpeed(float Speed)
{
	UWorld* World = Utils::GetWorldSafe();
	if (!World || !World->PersistentLevel) return;
	World->PersistentLevel->WorldSettings->TimeDilation = Speed;
}

void Cheats::AddCurrency(const std::string& type, int amount)
{
	if (!GVars.PlayerController || !GVars.PlayerController->IsA(AOakPlayerController::StaticClass())) return;
	AOakPlayerController* OakPC = static_cast<AOakPlayerController*>(GVars.PlayerController);
	if (!OakPC->CurrencyManager) return;

	if (!OakPC->CurrencyManager) return;

	for (int i = 0; i < OakPC->CurrencyManager->currencies.Num(); i++) {
		auto& cur = OakPC->CurrencyManager->currencies[i];
		if (cur.type.Name.ToString().find(type) != std::string::npos) {
			OakPC->Server_AddCurrency(cur.type, amount);
			break;
		}
	}
}

void Cheats::ClearGroundItems()
{
	if (!GVars.World || !GVars.Level) return;
	FVector farLoc = { 100000.f, 100000.f, -100000.f };
	
	for (int i = 0; i < GVars.Level->Actors.Num(); i++) {
		AActor* Actor = GVars.Level->Actors[i];
		if (Actor && Actor->IsA(AInventoryPickup::StaticClass())) {
			Actor->K2_SetActorLocation(farLoc, false, nullptr, false);
		}
	}
}

void Cheats::TeleportLoot()
{
	if (!GVars.World || !GVars.Level || !GVars.Character) return;
	
	FVector PlayerLoc = GVars.Character->K2_GetActorLocation();
	FRotator PlayerRot = GVars.Character->K2_GetActorRotation();
	
	PlayerLoc.Z -= 40.f;

	std::vector<AInventoryPickup*> Pickups;
	std::vector<AInventoryPickup*> Gear;

	for (int i = 0; i < GVars.Level->Actors.Num(); i++)
	{
		AActor* Actor = GVars.Level->Actors[i];
		if (Actor && Actor->IsA(AInventoryPickup::StaticClass()))
		{
			AInventoryPickup* Pickup = static_cast<AInventoryPickup*>(Actor);
			if (Pickup->GetName().find("Gear") != std::string::npos || Pickup->GetName().find("Weapon") != std::string::npos)
				Gear.push_back(Pickup);
			else
				Pickups.push_back(Pickup);
		}
	}

	for (auto p : Pickups)
	{
		p->K2_SetActorLocation(PlayerLoc, false, nullptr, false);
	}

	const int MAX_PER_CIRCLE = 35;
	const float CIRCLE_SPACING = 150.0f;
	const float BASE_RADIUS = 200.0f;

	int item_index = 0;
	float yaw_rad = PlayerRot.Yaw * (3.1415926535f / 180.0f);
	float forward_x = cosf(yaw_rad);
	float forward_y = sinf(yaw_rad);
	float right_x = -sinf(yaw_rad);
	float right_y = cosf(yaw_rad);

	for (auto g : Gear)
	{
		int ring = item_index / MAX_PER_CIRCLE;
		int pos_in_ring = item_index % MAX_PER_CIRCLE;

		float angle_offset = (2.0f * 3.1415926535f / MAX_PER_CIRCLE) * (float)pos_in_ring;
		float radius = BASE_RADIUS + (float)ring * CIRCLE_SPACING;

		float cos_a = cosf(angle_offset);
		float sin_a = sinf(angle_offset);

		float offset_x = (forward_x * radius * cos_a) + (right_x * radius * sin_a);
		float offset_y = (forward_y * radius * cos_a) + (right_y * radius * sin_a);

		FVector NewLoc = { PlayerLoc.X + offset_x, PlayerLoc.Y + offset_y, PlayerLoc.Z };
		
		FVector Dir = PlayerLoc - NewLoc;
		FRotator NewRot = Utils::VectorToRotation(Dir);
		NewRot.Pitch = 0;
		NewRot.Roll = 0;

		g->K2_SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, false);
		item_index++;
	}
}

void Cheats::SetExperienceLevel(int32 xpAmount)
{
	UWorld* World = Utils::GetWorldSafe();
	if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

	APlayerController* PC = World->OwningGameInstance->LocalPlayers[0]->PlayerController;
	if (!PC || !PC->IsA(AOakPlayerController::StaticClass())) return;

	AOakPlayerController* OakController = static_cast<AOakPlayerController*>(PC);
	AOakPlayerState* PS = OakController->GetOakPlayerState();

	if (PS)
	{
		if (PS->ExperienceState.Num() > 0)
		{
			PS->ExperienceState[0].ExperienceLevel = xpAmount;
		}
	}

	AOakCharacter* localChar = static_cast<AOakCharacter*>(OakController->Character);
	if (localChar && localChar->IsA(AOakCharacter::StaticClass()))
	{
		localChar->BroadcastLevelUp(xpAmount);
	}
}

template <typename T>
static T* GetWeaponBehavior(AWeapon* Weapon, UClass* BehaviorClass)
{
    if (!Weapon || !BehaviorClass) return nullptr;
    for (int i = 0; i < Weapon->behaviors.Num(); i++)
    {
        UWeaponBehavior* Behavior = Weapon->behaviors[i];
        if (Behavior && Behavior->IsA(BehaviorClass))
        {
            return static_cast<T*>(Behavior);
        }
    }
    return nullptr;
}

void Cheats::WeaponModifiers()
{
	if (!GVars.Character || Utils::bIsLoading) return;
	
	static std::unordered_map<UWeaponBehavior*, float> OrigProjSpeedMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigRecoilXMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigRecoilYMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigSwayMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigSwayAccMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigSwayZoomMap;
	static std::unordered_map<UWeaponBehavior*, float> OrigSwayZoomAccMap;

	for (uint8 i = 0; i < 4; i++)
	{
		AWeapon* weapon = UWeaponStatics::GetWeapon(GVars.Character, i);
		if (!weapon) continue;

		UWeaponBehavior_FireProjectile* projectileBehavior = GetWeaponBehavior<UWeaponBehavior_FireProjectile>(weapon, UWeaponBehavior_FireProjectile::StaticClass());
		if (projectileBehavior)
		{
			if (OrigProjSpeedMap.find(projectileBehavior) == OrigProjSpeedMap.end())
				OrigProjSpeedMap[projectileBehavior] = projectileBehavior->ProjectileSpeedScale.Value;

			if (WeaponSettings.InstantHitEnabled) {
				projectileBehavior->ProjectileSpeedScale.Value = WeaponSettings.ProjectileSpeedMultiplier;
			} else {
				projectileBehavior->ProjectileSpeedScale.Value = projectileBehavior->ProjectileSpeedScale.BaseValue; 
			}
		}

		UWeaponBehavior_Fire* fireBehavior = GetWeaponBehavior<UWeaponBehavior_Fire>(weapon, UWeaponBehavior_Fire::StaticClass());
		if (fireBehavior)
		{
			if (OrigRecoilXMap.find(fireBehavior) == OrigRecoilXMap.end()) {
				OrigRecoilXMap[fireBehavior] = fireBehavior->RecoilScaleX;
				OrigRecoilYMap[fireBehavior] = fireBehavior->RecoilScaleY;
			}

			if (WeaponSettings.RapidFireEnabled)
			{
				fireBehavior->firerate.Value = 999.0f * WeaponSettings.FireRate;
				fireBehavior->BurstFireDelay.Value = 0.01f / WeaponSettings.FireRate;
			}
			else
			{
				fireBehavior->firerate.Value = fireBehavior->firerate.BaseValue;
				fireBehavior->BurstFireDelay.Value = fireBehavior->BurstFireDelay.BaseValue;
			}

			if (WeaponSettings.NoRecoilEnabled)
			{
				float reduction = 1.0f - WeaponSettings.RecoilReduction;
				fireBehavior->spread.Value = fireBehavior->spread.BaseValue * reduction;
				fireBehavior->accuracyimpulse.Value = fireBehavior->accuracyimpulse.BaseValue * reduction;
				fireBehavior->BurstAccuracyImpulseScale.Value = fireBehavior->BurstAccuracyImpulseScale.BaseValue * reduction;
				fireBehavior->RecoilScale.Value = fireBehavior->RecoilScale.BaseValue * reduction;
				
				fireBehavior->RecoilScaleX = OrigRecoilXMap[fireBehavior] * reduction;
				fireBehavior->RecoilScaleY = OrigRecoilYMap[fireBehavior] * reduction;
			}
			else
			{
				fireBehavior->spread.Value = fireBehavior->spread.BaseValue;
				fireBehavior->accuracyimpulse.Value = fireBehavior->accuracyimpulse.BaseValue;
				fireBehavior->BurstAccuracyImpulseScale.Value = fireBehavior->BurstAccuracyImpulseScale.BaseValue;
				fireBehavior->RecoilScale.Value = fireBehavior->RecoilScale.BaseValue;

				fireBehavior->RecoilScaleX = OrigRecoilXMap[fireBehavior];
				fireBehavior->RecoilScaleY = OrigRecoilYMap[fireBehavior];
			}
		}

		UWeaponBehavior_Sway* swayBehavior = GetWeaponBehavior<UWeaponBehavior_Sway>(weapon, UWeaponBehavior_Sway::StaticClass());
		if (swayBehavior)
		{
			if (WeaponSettings.NoSwayEnabled)
			{
				swayBehavior->scale.Value = 0.0f;
				swayBehavior->AccuracyScale.Value = 0.0f;
				swayBehavior->ZoomScale.Value = 0.0f;
				swayBehavior->ZoomAccuracyScale.Value = 0.0f;
			}
			else
			{
				swayBehavior->scale.Value = swayBehavior->scale.BaseValue;
				swayBehavior->AccuracyScale.Value = swayBehavior->AccuracyScale.BaseValue;
				swayBehavior->ZoomScale.Value = swayBehavior->ZoomScale.BaseValue;
				swayBehavior->ZoomAccuracyScale.Value = swayBehavior->ZoomAccuracyScale.BaseValue;
			}
		}
	}
}


void Cheats::ChangeFOV()
{
	static float LastFOV = -1.0f;
	
	if (MiscSettings.EnableFOV && GVars.PlayerController)
	{
		if (LastFOV != MiscSettings.FOV) 
		{
			GVars.PlayerController->fov(MiscSettings.FOV);
			LastFOV = MiscSettings.FOV;
		}
		if (MiscSettings.EnableViewModelFOV)
		{
			// Optional: Try calling the SDK function or applying settings
			// A full implementation requires CameraModeState instance which we can hook/extract.
			// Currently relying on potential future hook to apply MiscSettings.ViewModelFOV
		}
	}
	else if (!MiscSettings.EnableFOV)
	{
		LastFOV = -1.0f; // Reset so when enabled again it sets fov
	}

	// Handle Continuous Third Person Enforcement and ADS
	if (GVars.PlayerController && GVars.Character && GVars.World)
	{
		AOakPlayerController* OakPC = static_cast<AOakPlayerController*>(GVars.PlayerController);
		AOakCharacter* OakChar = static_cast<AOakCharacter*>(GVars.Character);

		if (!OakPC->PlayerCameraManager) return;

		// Use static timer to avoid hammering transitions/spawns too fast
		static float LastTransitionTime = 0.0f;
		float CurrentTime = (float)ImGui::GetTime();

		SDK::FName CurrentMode = SDK::APlayerCameraModeManager::GetActorCameraMode(OakChar);
		std::string ModeStr = CurrentMode.ToString();
		bool bIsZooming = (uint8)OakChar->ZoomState.State != 0;

		// 1. Determine which camera to use
		bool bUseShadowCam = CVars.Freecam || (CVars.ThirdPerson && MiscSettings.ThirdPersonOTS);
		
		// 2. Spawn/Update Shadow Camera if needed
		if (bUseShadowCam)
		{
			if (!GVars.CameraActor || !SDK::UKismetSystemLibrary::IsValid(GVars.CameraActor))
			{
				SDK::FTransform SpawnTransform{};
				// Use current control rotation as initial direction
				SpawnTransform.Rotation = SDK::UKismetMathLibrary::Conv_RotatorToQuaternion(OakPC->GetControlRotation());
				SpawnTransform.Scale3D = { 1, 1, 1 };
				SpawnTransform.Translation = OakChar->K2_GetActorLocation();

				GVars.CameraActor = (SDK::ACameraActor*)SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(GVars.World, SDK::ACameraActor::StaticClass(), SpawnTransform, SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr, SDK::ESpawnActorScaleMethod::MultiplyWithRoot);
				if (GVars.CameraActor)
				{
					SDK::UGameplayStatics::FinishSpawningActor(GVars.CameraActor, SpawnTransform, SDK::ESpawnActorScaleMethod::MultiplyWithRoot);
				}
			}

			if (GVars.CameraActor && SDK::UKismetSystemLibrary::IsValid(GVars.CameraActor))
			{
				if (CVars.Freecam)
				{
					if (GVars.CameraActor->GetAttachParentActor())
						GVars.CameraActor->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
					
					float FlySpeed = 20.0f;
					if (GetAsyncKeyState(VK_CONTROL) & 0x8000) FlySpeed *= 3.0f;

					SDK::FVector CamLoc = GVars.CameraActor->K2_GetActorLocation();
					SDK::FRotator CamRot = OakPC->GetControlRotation(); 
					GVars.CameraActor->K2_SetActorRotation(CamRot, false);
					
					SDK::FVector Forward = Utils::FRotatorToVector(CamRot);
					SDK::FVector Right = Utils::FRotatorToVector({0, CamRot.Yaw + 90.0, 0});
					
					if (GetAsyncKeyState('W') & 0x8000) { CamLoc.X += Forward.X * FlySpeed; CamLoc.Y += Forward.Y * FlySpeed; CamLoc.Z += Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('S') & 0x8000) { CamLoc.X -= Forward.X * FlySpeed; CamLoc.Y -= Forward.Y * FlySpeed; CamLoc.Z -= Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('D') & 0x8000) { CamLoc.X += Right.X * FlySpeed;   CamLoc.Y += Right.Y * FlySpeed;   CamLoc.Z += Right.Z * FlySpeed;   }
					if (GetAsyncKeyState('A') & 0x8000) { CamLoc.X -= Right.X * FlySpeed;   CamLoc.Y -= Right.Y * FlySpeed;   CamLoc.Z -= Right.Z * FlySpeed;   }
					if (GetAsyncKeyState(VK_SPACE) & 0x8000) CamLoc.Z += FlySpeed;
					if (GetAsyncKeyState(VK_SHIFT) & 0x8000) CamLoc.Z -= FlySpeed;
					
					GVars.CameraActor->K2_SetActorLocation(CamLoc, false, nullptr, false);

					if (MiscSettings.FreecamBlockInput)
					{
						OakPC->SetIgnoreMoveInput(true);
						OakPC->SetIgnoreLookInput(true);
					}
					else
					{
						if (OakPC->IsMoveInputIgnored()) OakPC->SetIgnoreMoveInput(false);
						if (OakPC->IsLookInputIgnored()) OakPC->SetIgnoreLookInput(false);
					}

					if (OakPC->PlayerCameraManager->ViewTarget.target != GVars.CameraActor)
					{
						OakPC->SetViewTargetWithBlend(GVars.CameraActor, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
				else // OTS Mode
				{
					if (OakPC->IsMoveInputIgnored()) OakPC->SetIgnoreMoveInput(false);
					if (OakPC->IsLookInputIgnored()) OakPC->SetIgnoreLookInput(false);

					// Ensure background native is in ThirdPerson to render character
					if (ModeStr.find("ThirdPerson") == std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
					{
						OakPC->CameraTransition(SDK::UKismetStringLibrary::Conv_StringToName(L"ThirdPerson"), SDK::UKismetStringLibrary::Conv_StringToName(L"Default"), 0.0f, false, false);
						LastTransitionTime = CurrentTime;
					}

					if (GVars.CameraActor->GetAttachParentActor() != OakChar)
					{
						GVars.CameraActor->K2_AttachToActor(OakChar, SDK::FName(), SDK::EAttachmentRule::KeepRelative, SDK::EAttachmentRule::KeepRelative, SDK::EAttachmentRule::KeepRelative, false);
					}
					
					SDK::FVector Offset;
					Offset.X = MiscSettings.OTS_X;
					Offset.Y = MiscSettings.OTS_Y;
					Offset.Z = 65.0f + MiscSettings.OTS_Z; 
					
					GVars.CameraActor->K2_SetActorRelativeLocation(Offset, false, nullptr, false);
					GVars.CameraActor->K2_SetActorRotation(OakPC->GetControlRotation(), false);

					if (OakPC->PlayerCameraManager->ViewTarget.target != GVars.CameraActor)
					{
						OakPC->SetViewTargetWithBlend(GVars.CameraActor, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
			}
		}
		else // Native Camera Path
		{
			if (OakPC->IsMoveInputIgnored()) OakPC->SetIgnoreMoveInput(false);
			if (OakPC->IsLookInputIgnored()) OakPC->SetIgnoreLookInput(false);

			bool bShouldBeInThirdPerson = CVars.ThirdPerson;
			
			if (CVars.ThirdPerson && !MiscSettings.ThirdPersonOTS && bIsZooming && MiscSettings.ThirdPersonADSFirstPerson)
			{
				bShouldBeInThirdPerson = false;
			}

			if (OakPC->PlayerCameraManager->ViewTarget.target != OakChar)
			{
				OakPC->SetViewTargetWithBlend(OakChar, 0.15f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
			}

			if (bShouldBeInThirdPerson)
			{
				if (ModeStr.find("ThirdPerson") == std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(SDK::UKismetStringLibrary::Conv_StringToName(L"ThirdPerson"), SDK::UKismetStringLibrary::Conv_StringToName(L"Default"), 0.15f, false, false);
					LastTransitionTime = CurrentTime;
				}
				
				// Background Sync for Shadow Camera
				if (GVars.CameraActor && SDK::UKismetSystemLibrary::IsValid(GVars.CameraActor))
				{
					if (GVars.CameraActor->GetAttachParentActor())
						GVars.CameraActor->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
					
					// Use player camera manager for source location
					GVars.CameraActor->K2_SetActorLocationAndRotation(OakPC->PlayerCameraManager->CameraCachePrivate.POV.Location, OakPC->PlayerCameraManager->CameraCachePrivate.POV.Rotation, false, nullptr, false);
				}
			}
			else
			{
				if (ModeStr.find("ThirdPerson") != std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(SDK::UKismetStringLibrary::Conv_StringToName(L"FirstPerson"), SDK::UKismetStringLibrary::Conv_StringToName(L"Default"), 0.15f, false, false);
					LastTransitionTime = CurrentTime;
				}
			}
		}

		// FOV Slider
		if (MiscSettings.EnableFOV)
		{
			OakPC->PlayerCameraManager->DefaultFOV = MiscSettings.FOV;
			if (GVars.CameraActor && SDK::UKismetSystemLibrary::IsValid(GVars.CameraActor) && GVars.CameraActor->CameraComponent)
			{
				GVars.CameraActor->CameraComponent->SetFieldOfView(MiscSettings.FOV);
			}
		}
	}
}

void Cheats::ChangeGameRenderSettings()
{
	if (!GVars.PlayerController || !GVars.World) return;
	
	static bool ShouldDisableClouds = false;
	if (MiscSettings.DisableVolumetricClouds != ShouldDisableClouds)
	{
		ShouldDisableClouds = MiscSettings.DisableVolumetricClouds;
		SDK::FString cmd = ShouldDisableClouds ? L"r.VolumetricCloud 0" : L"r.VolumetricCloud 1";
		SDK::UKismetSystemLibrary::ExecuteConsoleCommand(Utils::GetWorldSafe(), cmd, GVars.PlayerController);
	}
}


void Cheats::RenderEnabledOptions()
{
	if (!CVars.RenderOptions) return;
	float Hue = fmodf((float)ImGui::GetTime() * 0.2f, 1.0f); // cycles every 5s
	ImVec4 Color = ImColor::HSV(Hue, 1.f, 1.f);

	ImGui::SetNextWindowBgAlpha(0.3f);

	ImGui::Begin(Localization::T("ACTIVE_FEATURES_LIST"), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar);

	ImGui::SetWindowPos(ImVec2(10, 30));

	ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), Localization::T("ACTIVE_FEATURES"));
	if (CVars.GodMode)
		ImGui::TextColored(Color, Localization::T("GODMODE"));
	if (CVars.InfAmmo)
		ImGui::TextColored(Color, Localization::T("INF_AMMO"));
	if (CVars.Aimbot)
		ImGui::TextColored(Color, Localization::T("AIMBOT"));
	if (CVars.ESP)
		ImGui::TextColored(Color, Localization::T("ESP"));
	if (CVars.SpeedEnabled)
		ImGui::TextColored(Color, Localization::T("SPEED_X_1F"), CVars.Speed);
	if (CVars.FlightEnabled)
		ImGui::TextColored(Color, Localization::T("FLIGHT"));
	if (CVars.SilentAim)
		ImGui::TextColored(Color, Localization::T("SILENT_AIM"));
	if (CVars.NoTarget)
		ImGui::TextColored(Color, Localization::T("NO_TARGET"));
	if (CVars.Demigod)
		ImGui::TextColored(Color, Localization::T("DEMIGOD"));
	if (CVars.PlayersOnly)
		ImGui::TextColored(Color, Localization::T("PLAYERS_ONLY"));
	if (CVars.GameSpeed != 1.0f)
		ImGui::TextColored(Color, "%s: x%.1f", Localization::T("GAME_SPEED"), CVars.GameSpeed);
	if (CVars.ThirdPerson)
		ImGui::TextColored(Color, Localization::T("THIRD_PERSON"));

	CheatOptionsWindowSize = ImGui::GetWindowSize();

	ImGui::End();
}

void Cheats::ToggleThirdPerson()
{
	CVars.ThirdPerson = !CVars.ThirdPerson;
	if (CVars.ThirdPerson) CVars.Freecam = false;
}

void Cheats::ToggleFreecam()
{
	CVars.Freecam = !CVars.Freecam;
	if (CVars.Freecam) CVars.ThirdPerson = false;
}

void Cheats::InfiniteAmmo()
{
	if (GVars.PlayerController && GVars.PlayerController->IsA(AOakPlayerController::StaticClass()))
		static_cast<AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Loaded);
}


void Cheats::EnforcePersistence()
{
	if (Utils::bIsLoading || !GVars.PlayerController || !GVars.Character) return;

	if (CVars.GodMode) GVars.Character->bCanBeDamaged = false;

	static bool lastNoTarget = false;
	if (CVars.NoTarget != lastNoTarget) {
		Cheats::ToggleNoTarget();
		lastNoTarget = CVars.NoTarget;
	}
}
