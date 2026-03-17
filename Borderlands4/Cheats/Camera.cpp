#include "pch.h"

namespace
{
	enum class EShadowCameraMode : uint8
	{
		None,
		Freecam,
		OTS
	};

	EShadowCameraMode g_ShadowCameraMode = EShadowCameraMode::None;
	SDK::USpringArmComponent* g_ShadowSpringArm = nullptr;
	float g_SmoothedShadowFOV = -1.0f;
	float g_SmoothedOTSADSBlend = 0.0f;
	bool g_FreecamRotationInitialized = false;
	SDK::FRotator g_FreecamRotation{};

	constexpr float kOTSViewPivotZ = 65.0f;
	constexpr float kOTSMaxArmLength = 900.0f;
	constexpr float kOTSCollisionRadius = 12.0f;
	constexpr double kOTSMaxWorldCoord = 2000000.0;
	constexpr float kOTSCameraLagSpeed = 16.0f;
	constexpr float kOTSCameraLagSpeedADS = 20.0f;
	constexpr float kOTSCameraLagMaxTimeStep = 1.0f / 60.0f;
	constexpr float kFreecamMouseSensitivity = 0.12f;

	struct FOTSState
	{
		float OffsetX;
		float OffsetY;
		float OffsetZ;
		float FOV;
	};

	bool IsValidShadowCamera(SDK::ACameraActor* CameraActor);
	float GetShadowCameraBaseFOV();

	bool IsFiniteVector(const SDK::FVector& value)
	{
		return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
	}

	bool IsReasonableWorldLocation(const SDK::FVector& value)
	{
		if (!IsFiniteVector(value))
			return false;

		return std::abs(value.X) <= kOTSMaxWorldCoord
			&& std::abs(value.Y) <= kOTSMaxWorldCoord
			&& std::abs(value.Z) <= kOTSMaxWorldCoord;
	}

	bool IsValidSceneComponent(SDK::USceneComponent* Component)
	{
		if (!Component) return false;

		__try
		{
			if (IsBadReadPtr(Component, sizeof(void*)) || !Component->VTable) return false;
			if (!Component->Class || IsBadReadPtr(Component->Class, sizeof(void*))) return false;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool IsValidSpringArm(SDK::USpringArmComponent* SpringArm)
	{
		if (!IsValidSceneComponent(SpringArm)) return false;

		__try
		{
			return SpringArm->IsA(SDK::USpringArmComponent::StaticClass());
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	SDK::FTransform MakeIdentityTransform()
	{
		SDK::FTransform transform{};
		transform.Rotation = SDK::UKismetMathLibrary::Conv_RotatorToQuaternion({ 0.0, 0.0, 0.0 });
		transform.Translation = { 0.0, 0.0, 0.0 };
		transform.Scale3D = { 1.0, 1.0, 1.0 };
		return transform;
	}

	void ResetShadowSpringArm()
	{
		g_ShadowSpringArm = nullptr;
		g_ShadowCameraMode = EShadowCameraMode::None;
	}

	SDK::FRotator UpdateFreecamRotation(const SDK::FRotator& fallbackRotation)
	{
		if (!g_FreecamRotationInitialized)
		{
			g_FreecamRotation = fallbackRotation;
			g_FreecamRotationInitialized = true;
		}

		if (!GUI::ShowMenu)
		{
			const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
			const double yawDelta = static_cast<double>(mouseDelta.x) * static_cast<double>(kFreecamMouseSensitivity);
			const double pitchDelta = static_cast<double>(mouseDelta.y) * static_cast<double>(kFreecamMouseSensitivity);
			g_FreecamRotation.Yaw += yawDelta;
			g_FreecamRotation.Pitch = (std::clamp)(g_FreecamRotation.Pitch - pitchDelta, -89.0, 89.0);
			g_FreecamRotation.Roll = 0.0f;
		}

		return g_FreecamRotation;
	}

	float GetFrameDeltaSeconds()
	{
		const float currentTime = static_cast<float>(ImGui::GetTime());
		static float lastObservedTime = -1.0f;
		static float lastDeltaSeconds = 1.0f / 60.0f;
		if (std::abs(currentTime - lastObservedTime) < 0.0001f)
			return lastDeltaSeconds;

		static float lastTime = currentTime;
		float deltaSeconds = currentTime - lastTime;
		lastTime = currentTime;
		if (deltaSeconds < 0.0f || deltaSeconds > 0.25f)
			deltaSeconds = 1.0f / 60.0f;
		lastObservedTime = currentTime;
		lastDeltaSeconds = deltaSeconds;
		return deltaSeconds;
	}

	float UpdateSmoothedBlend(float currentBlend, bool bShouldZoom)
	{
		const float duration = std::clamp(ConfigManager::F("Misc.OTSADSBlendTime"), 0.01f, 2.0f);
		const float targetBlend = bShouldZoom ? 1.0f : 0.0f;
		const float alpha = std::clamp(GetFrameDeltaSeconds() / duration, 0.0f, 1.0f);
		return currentBlend + ((targetBlend - currentBlend) * alpha);
	}

	SDK::USpringArmComponent* EnsureShadowSpringArm(SDK::ACameraActor* Cam)
	{
		if (!IsValidShadowCamera(Cam) || !IsValidSceneComponent(Cam->RootComponent) || !IsValidSceneComponent(Cam->CameraComponent))
			return nullptr;

		if (!IsValidSpringArm(g_ShadowSpringArm))
			g_ShadowSpringArm = nullptr;

		if (!g_ShadowSpringArm)
		{
			SDK::UActorComponent* AddedComponent = Cam->AddComponentByClass(SDK::USpringArmComponent::StaticClass(), true, MakeIdentityTransform(), false);
			if (!AddedComponent || !AddedComponent->IsA(SDK::USpringArmComponent::StaticClass()))
				return nullptr;

			g_ShadowSpringArm = static_cast<SDK::USpringArmComponent*>(AddedComponent);
		}

		if (!IsValidSpringArm(g_ShadowSpringArm))
			return nullptr;

		if (g_ShadowSpringArm->GetAttachParent() != Cam->RootComponent)
		{
			g_ShadowSpringArm->K2_AttachToComponent(
				Cam->RootComponent,
				SDK::FName(),
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				false);
		}

		if (Cam->CameraComponent->GetAttachParent() != g_ShadowSpringArm)
		{
			Cam->CameraComponent->K2_AttachToComponent(
				g_ShadowSpringArm,
				SDK::FName(),
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				false);
		}

		Cam->CameraComponent->K2_SetRelativeLocation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
		Cam->CameraComponent->K2_SetRelativeRotation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
		return g_ShadowSpringArm;
	}

	void ConfigureSpringArmForOTS(
		SDK::USpringArmComponent* SpringArm,
		const SDK::FRotator& ControlRot,
		float OffsetX,
		float OffsetY,
		float OffsetZ,
		bool bIsZooming)
	{
		if (!IsValidSpringArm(SpringArm))
			return;

		const float clampedArmLength = std::clamp(-OffsetX, 0.0f, kOTSMaxArmLength);

		SpringArm->TargetArmLength = clampedArmLength;
		SpringArm->TargetOffset = { 0.0, 0.0, kOTSViewPivotZ + OffsetZ };
		SpringArm->SocketOffset = { OffsetX + clampedArmLength, OffsetY, 0.0 };
		SpringArm->ProbeSize = kOTSCollisionRadius;
		SpringArm->ProbeChannel = SDK::ECollisionChannel::ECC_Camera;
		SpringArm->bDoCollisionTest = true;
		SpringArm->bUsePawnControlRotation = false;
		SpringArm->bInheritPitch = false;
		SpringArm->bInheritYaw = false;
		SpringArm->bInheritRoll = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->bEnableCameraRotationLag = false;
		SpringArm->bUseCameraLagSubstepping = true;
		SpringArm->CameraLagSpeed = bIsZooming ? kOTSCameraLagSpeedADS : kOTSCameraLagSpeed;
		SpringArm->CameraLagMaxTimeStep = kOTSCameraLagMaxTimeStep;
		SpringArm->CameraLagMaxDistance = 0.0f;
		SpringArm->K2_SetRelativeLocation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
		SpringArm->K2_SetRelativeRotation(ControlRot, false, nullptr, false);
	}

	void CollapseSpringArmForFreecam(SDK::ACameraActor* Cam, SDK::USpringArmComponent* SpringArm)
	{
		if (!IsValidShadowCamera(Cam) || !IsValidSpringArm(SpringArm))
			return;

		const SDK::FVector currentCamLoc = Cam->CameraComponent->K2_GetComponentLocation();
		const SDK::FRotator currentCamRot = Cam->CameraComponent->K2_GetComponentRotation();

		SpringArm->bEnableCameraLag = false;
		SpringArm->bEnableCameraRotationLag = false;
		SpringArm->bDoCollisionTest = false;
		SpringArm->TargetArmLength = 0.0f;
		SpringArm->TargetOffset = { 0.0, 0.0, 0.0 };
		SpringArm->SocketOffset = { 0.0, 0.0, 0.0 };
		SpringArm->K2_SetRelativeLocation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
		SpringArm->K2_SetRelativeRotation({ 0.0, 0.0, 0.0 }, false, nullptr, false);

		if (IsReasonableWorldLocation(currentCamLoc))
			Cam->K2_SetActorLocationAndRotation(currentCamLoc, currentCamRot, false, nullptr, false);
	}

	void SyncShadowCameraAttachmentForOTS(SDK::ACameraActor* Cam, SDK::AOakCharacter* OakChar)
	{
		if (!IsValidShadowCamera(Cam) || !OakChar)
			return;

		if (Cam->owner != OakChar)
		{
			Cam->SetOwner(OakChar);
		}

		if (Cam->GetAttachParentActor() != OakChar)
		{
			Cam->K2_AttachToActor(
				OakChar,
				SDK::FName(),
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				SDK::EAttachmentRule::KeepRelative,
				false);
		}

		Cam->K2_SetActorRelativeLocation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
		Cam->K2_SetActorRelativeRotation({ 0.0, 0.0, 0.0 }, false, nullptr, false);
	}

	bool IsZoomingNow()
	{
		if (!GVars.Character || !GVars.Character->IsA(SDK::AOakCharacter::StaticClass())) return false;
		const SDK::AOakCharacter* oakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		return ((uint8)oakChar->ZoomState.State != 0);
	}

	float GetShadowCameraBaseFOV()
	{
		if (ConfigManager::B("Misc.EnableFOV"))
		{
			return ConfigManager::F("Misc.FOV");
		}

		if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
		{
			const SDK::AOakPlayerController* oakPc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
			if (oakPc->PlayerCameraManager)
			{
				return oakPc->PlayerCameraManager->DefaultFOV;
			}
		}

		if (GVars.POV)
		{
			return GVars.POV->fov;
		}

		return 90.0f;
	}

	float GetCurrentOTSADSBlend(bool bIsZooming)
	{
		if (!ConfigManager::B("Misc.OTSADSOverride"))
			return 0.0f;

		g_SmoothedOTSADSBlend = UpdateSmoothedBlend(g_SmoothedOTSADSBlend, bIsZooming);
		return g_SmoothedOTSADSBlend;
	}

	FOTSState GetBlendedOTSState(float blendAlpha)
	{
		const FOTSState baseState{
			ConfigManager::F("Misc.OTS_X"),
			ConfigManager::F("Misc.OTS_Y"),
			ConfigManager::F("Misc.OTS_Z"),
			GetShadowCameraBaseFOV()
		};

		if (!ConfigManager::B("Misc.OTSADSOverride"))
			return baseState;

		const FOTSState adsState{
			ConfigManager::F("Misc.OTSADS_X"),
			ConfigManager::F("Misc.OTSADS_Y"),
			ConfigManager::F("Misc.OTSADS_Z"),
			std::clamp(ConfigManager::F("Misc.OTSADSFOV"), 20.0f, 180.0f)
		};

		auto blend = [](float from, float to, float alpha)
		{
			return from + ((to - from) * alpha);
		};

		return {
			blend(baseState.OffsetX, adsState.OffsetX, blendAlpha),
			blend(baseState.OffsetY, adsState.OffsetY, blendAlpha),
			blend(baseState.OffsetZ, adsState.OffsetZ, blendAlpha),
			blend(baseState.FOV, adsState.FOV, blendAlpha)
		};
	}

	float GetAppliedFOV(bool bForShadowCamera)
	{
		float fov = bForShadowCamera ? GetBlendedOTSState(g_SmoothedOTSADSBlend).FOV : ConfigManager::F("Misc.FOV");
		if (IsZoomingNow())
		{
			if (!bForShadowCamera && ConfigManager::B("Misc.EnableFOV"))
			{
				fov *= std::clamp(ConfigManager::F("Misc.ADSFOVScale"), 0.2f, 1.0f);
			}
		}
		return std::clamp(fov, 20.0f, 180.0f);
	}

	float UpdateSmoothedShadowFOV(float targetFOV)
	{
		if (g_SmoothedShadowFOV < 0.0f || !ConfigManager::B("Player.OverShoulder"))
		{
			g_SmoothedShadowFOV = targetFOV;
			return g_SmoothedShadowFOV;
		}

		const float alpha = std::clamp(GetFrameDeltaSeconds() / std::clamp(ConfigManager::F("Misc.OTSADSBlendTime"), 0.01f, 2.0f), 0.0f, 1.0f);
		g_SmoothedShadowFOV += (targetFOV - g_SmoothedShadowFOV) * alpha;
		return g_SmoothedShadowFOV;
	}

	void ApplyPlayerFOV(float targetFOV)
	{
		if (!GVars.PlayerController) return;
		GVars.PlayerController->fov(targetFOV);

		if (!GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			return;

		SDK::AOakPlayerController* oakPc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		if (!oakPc || !oakPc->PlayerCameraManager) return;

		oakPc->PlayerCameraManager->DefaultFOV = targetFOV;

		if (oakPc->PlayerCameraManager->IsA(SDK::APlayerCameraModeManager::StaticClass()))
		{
			SDK::APlayerCameraModeManager* modeMgr = static_cast<SDK::APlayerCameraModeManager*>(oakPc->PlayerCameraManager);
			if (modeMgr->CameraModeState)
			{
				modeMgr->CameraModeState->SetBaseFOV(targetFOV, true);
			}
		}
	}

	void ApplyViewModelFOV()
	{
		if (!ConfigManager::B("Misc.EnableViewModelFOV")) return;
		if (!GVars.PlayerController) return;
		if (!GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass())) return;

		SDK::AOakPlayerController* pc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		if (!pc->PlayerCameraManager) return;
		if (!pc->PlayerCameraManager->IsA(SDK::APlayerCameraModeManager::StaticClass())) return;

		SDK::APlayerCameraModeManager* cameraMode = static_cast<SDK::APlayerCameraModeManager*>(pc->PlayerCameraManager);
		if (!cameraMode || !cameraMode->CameraModeState) return;

		const float newViewModelValue = std::clamp(ConfigManager::F("Misc.ViewModelFOV"), 60.0f, 150.0f);
		cameraMode->CameraModeState->SetViewModelFOV(newViewModelValue, true);
	}

	bool IsValidShadowCamera(SDK::ACameraActor* CameraActor)
	{
		if (!CameraActor) return false;
		if (!Utils::IsValidActor(CameraActor)) return false;

		__try
		{
			if (!CameraActor->IsA(SDK::ACameraActor::StaticClass())) return false;
			if (!CameraActor->CameraComponent) return false;
			if (IsBadReadPtr(CameraActor->CameraComponent, sizeof(void*))) return false;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	SDK::ACameraActor* EnsureShadowCamera(SDK::AOakPlayerController* OakPC, SDK::AOakCharacter* OakChar)
	{
		SDK::ACameraActor* CachedCamera = nullptr;
		{
			std::scoped_lock GVarsLock(gGVarsMutex);
			CachedCamera = GVars.CameraActor;
		}
		if (IsValidShadowCamera(CachedCamera))
			return CachedCamera;

		SDK::FTransform SpawnTransform{};
		SpawnTransform.Rotation = SDK::UKismetMathLibrary::Conv_RotatorToQuaternion(OakPC->GetControlRotation());
		SpawnTransform.Translation = OakChar->K2_GetActorLocation();
		SpawnTransform.Scale3D = { 1.0, 1.0, 1.0 };

		auto* Spawned = SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(
			GVars.World,
			SDK::ACameraActor::StaticClass(),
			SpawnTransform,
			SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			nullptr,
			SDK::ESpawnActorScaleMethod::MultiplyWithRoot);

		if (!Spawned) return nullptr;

		SDK::ACameraActor* SpawnedCamera = static_cast<SDK::ACameraActor*>(
			SDK::UGameplayStatics::FinishSpawningActor(Spawned, SpawnTransform, SDK::ESpawnActorScaleMethod::MultiplyWithRoot));

		if (IsValidShadowCamera(SpawnedCamera))
		{
			ResetShadowSpringArm();
			std::scoped_lock GVarsLock(gGVarsMutex);
			GVars.CameraActor = SpawnedCamera;
			return SpawnedCamera;
		}

		ResetShadowSpringArm();
		std::scoped_lock GVarsLock(gGVarsMutex);
		GVars.CameraActor = nullptr;
		return nullptr;
	}

	void UpdateCameraModes()
	{
		if (!GVars.PlayerController || !GVars.Character || !GVars.World) return;
		SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		if (!OakPC || !OakChar || !OakPC->PlayerCameraManager) return;
		if (!Utils::IsValidActor(OakPC) || !Utils::IsValidActor(OakChar)) return;

		static float LastTransitionTime = 0.0f;
		const float CurrentTime = (float)ImGui::GetTime();
		const bool bIsZooming = ((uint8)OakChar->ZoomState.State != 0);
		const bool bUseShadowCam = ConfigManager::B("Player.Freecam") || ConfigManager::B("Player.OverShoulder");

		if (bUseShadowCam)
		{
			SDK::ACameraActor* Cam = EnsureShadowCamera(OakPC, OakChar);
			if (Cam)
			{
				SDK::USpringArmComponent* SpringArm = EnsureShadowSpringArm(Cam);
				if (ConfigManager::B("Player.Freecam"))
				{
					g_SmoothedOTSADSBlend = 0.0f;
					if (SpringArm && g_ShadowCameraMode != EShadowCameraMode::Freecam)
					{
						CollapseSpringArmForFreecam(Cam, SpringArm);
					}
					g_ShadowCameraMode = EShadowCameraMode::Freecam;

					if (Cam->GetAttachParentActor())
					{
						Cam->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
					}

					float FlySpeed = 20.0f;
					if (GetAsyncKeyState(VK_CONTROL) & 0x8000) FlySpeed *= 3.0f;

					SDK::FVector CamLoc = Cam->K2_GetActorLocation();
					SDK::FRotator CamRot = UpdateFreecamRotation(Cam->K2_GetActorRotation());
					Cam->K2_SetActorRotation(CamRot, false);

					SDK::FVector Forward = Utils::FRotatorToVector(CamRot);
					SDK::FVector Right = Utils::FRotatorToVector({ 0, CamRot.Yaw + 90.0, 0 });

					if (GetAsyncKeyState('W') & 0x8000) { CamLoc.X += Forward.X * FlySpeed; CamLoc.Y += Forward.Y * FlySpeed; CamLoc.Z += Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('S') & 0x8000) { CamLoc.X -= Forward.X * FlySpeed; CamLoc.Y -= Forward.Y * FlySpeed; CamLoc.Z -= Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('D') & 0x8000) { CamLoc.X += Right.X * FlySpeed; CamLoc.Y += Right.Y * FlySpeed; CamLoc.Z += Right.Z * FlySpeed; }
					if (GetAsyncKeyState('A') & 0x8000) { CamLoc.X -= Right.X * FlySpeed; CamLoc.Y -= Right.Y * FlySpeed; CamLoc.Z -= Right.Z * FlySpeed; }
					if (GetAsyncKeyState(VK_SPACE) & 0x8000) CamLoc.Z += FlySpeed;
					if (GetAsyncKeyState(VK_SHIFT) & 0x8000) CamLoc.Z -= FlySpeed;

					if (IsReasonableWorldLocation(CamLoc))
					{
						Cam->K2_SetActorLocation(CamLoc, false, nullptr, false);
					}

					if (OakPC->PlayerCameraManager->ViewTarget.target != Cam)
					{
						OakPC->SetViewTargetWithBlend(Cam, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
				else
				{
					if (!SpringArm) return;

					const std::string ModeStr = SDK::APlayerCameraModeManager::GetActorCameraMode(OakChar).ToString();
					if (ModeStr.find("ThirdPerson") == std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
					{
						OakPC->CameraTransition(
							SDK::UKismetStringLibrary::Conv_StringToName(L"ThirdPerson"),
							SDK::UKismetStringLibrary::Conv_StringToName(L"Default"),
							0.0f,
							false,
							false);
						LastTransitionTime = CurrentTime;
					}

					const SDK::FRotator ControlRot = OakPC->GetControlRotation();
					const float otsADSBlend = GetCurrentOTSADSBlend(bIsZooming);
					const FOTSState otsState = GetBlendedOTSState(otsADSBlend);
					SyncShadowCameraAttachmentForOTS(Cam, OakChar);
					ConfigureSpringArmForOTS(
						SpringArm,
						ControlRot,
						otsState.OffsetX,
						otsState.OffsetY,
						otsState.OffsetZ,
						bIsZooming);
					g_ShadowCameraMode = EShadowCameraMode::OTS;

					if (OakPC->PlayerCameraManager->ViewTarget.target != Cam)
					{
						OakPC->SetViewTargetWithBlend(Cam, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
			}
		}
		else
		{
			g_ShadowCameraMode = EShadowCameraMode::None;
			g_SmoothedShadowFOV = -1.0f;
			g_SmoothedOTSADSBlend = 0.0f;
			g_FreecamRotationInitialized = false;

			bool bShouldBeInThirdPerson = ConfigManager::B("Player.ThirdPerson");
			if (ConfigManager::B("Player.ThirdPerson") && bIsZooming && ConfigManager::B("Misc.ThirdPersonADSFirstPerson"))
			{
				bShouldBeInThirdPerson = false;
			}

			if (OakPC->PlayerCameraManager->ViewTarget.target != OakChar)
			{
				OakPC->SetViewTargetWithBlend(OakChar, 0.15f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
			}

			const std::string ModeStr = SDK::APlayerCameraModeManager::GetActorCameraMode(OakChar).ToString();
			if (bShouldBeInThirdPerson)
			{
				if (ModeStr.find("ThirdPerson") == std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(
						SDK::UKismetStringLibrary::Conv_StringToName(L"ThirdPerson"),
						SDK::UKismetStringLibrary::Conv_StringToName(L"Default"),
						0.15f,
						false,
						false);
					LastTransitionTime = CurrentTime;
				}
			}
			else
			{
				if (ModeStr.find("ThirdPerson") != std::string::npos && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(
						SDK::UKismetStringLibrary::Conv_StringToName(L"FirstPerson"),
						SDK::UKismetStringLibrary::Conv_StringToName(L"Default"),
						0.15f,
						false,
						false);
					LastTransitionTime = CurrentTime;
				}
			}
		}
	}
}

void Cheats::ToggleThirdPerson()
{
	ConfigManager::B("Player.ThirdPerson") = !ConfigManager::B("Player.ThirdPerson");
	if (ConfigManager::B("Player.ThirdPerson"))
	{
		ConfigManager::B("Player.Freecam") = false;
		ConfigManager::B("Player.OverShoulder") = false;
	}
}

void Cheats::ToggleFreecam()
{
	ConfigManager::B("Player.Freecam") = !ConfigManager::B("Player.Freecam");
	if (ConfigManager::B("Player.Freecam"))
	{
		ConfigManager::B("Player.ThirdPerson") = false;
		ConfigManager::B("Player.OverShoulder") = false;
	}
}

void Cheats::ChangeFOV()
{
	if (!GVars.PlayerController) return;

	static float LastFOV = -1.0f;
	if (ConfigManager::B("Misc.EnableFOV"))
	{
		const float targetFOV = GetAppliedFOV(false);
		// ADS often overrides FOV every frame, so force-apply while zooming.
		if (LastFOV != targetFOV || IsZoomingNow())
		{
			ApplyPlayerFOV(targetFOV);
			LastFOV = targetFOV;
		}
	}
	else
	{
		LastFOV = -1.0f;
	}
}

void Cheats::ChangeGameRenderSettings()
{
	if (!GVars.PlayerController) return;
	static bool ShouldDisableClouds = false;
	if (ConfigManager::B("Misc.DisableVolumetricClouds") != ShouldDisableClouds) {
		ShouldDisableClouds = ConfigManager::B("Misc.DisableVolumetricClouds");
		SDK::FString cmd = ShouldDisableClouds ? L"r.VolumetricCloud 0" : L"r.VolumetricCloud 1";
		SDK::UKismetSystemLibrary::ExecuteConsoleCommand(Utils::GetWorldSafe(), cmd, GVars.PlayerController);
	}
}

void Cheats::UpdateCamera()
{
	Cheats::ChangeFOV();
	UpdateCameraModes();

	SDK::ACameraActor* Cam = nullptr;
	{
		std::scoped_lock GVarsLock(gGVarsMutex);
		Cam = GVars.CameraActor;
	}
	if (IsValidShadowCamera(Cam) && Cam->CameraComponent)
	{
		const float targetShadowFOV = GetAppliedFOV(true);
		Cam->CameraComponent->SetFieldOfView(UpdateSmoothedShadowFOV(targetShadowFOV));
	}
	else
	{
		g_SmoothedShadowFOV = -1.0f;
	}

	if (ConfigManager::B("Misc.EnableFOV"))
	{
		const float appliedFOV = GetAppliedFOV(false);
		ApplyPlayerFOV(appliedFOV);
	}
	ApplyViewModelFOV();

	Cheats::ChangeGameRenderSettings();
}

bool Cheats::HandleCameraEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	(void)Object;
	(void)Function;
	(void)Params;
	return false;
}
