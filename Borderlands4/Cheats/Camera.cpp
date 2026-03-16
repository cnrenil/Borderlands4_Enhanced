#include "pch.h"

namespace
{
	bool g_OTSSmoothInitialized = false;
	SDK::FVector g_OTSSmoothedLoc{};
	float g_SmoothedShadowFOV = -1.0f;

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

	float GetAppliedFOV(bool bForShadowCamera)
	{
		float fov = bForShadowCamera ? GetShadowCameraBaseFOV() : ConfigManager::F("Misc.FOV");
		if (IsZoomingNow())
		{
			if (bForShadowCamera && ConfigManager::B("Player.OverShoulder") && ConfigManager::B("Misc.OTSADSFOVBoost"))
			{
				fov *= std::clamp(ConfigManager::F("Misc.OTSADSFOVScale"), 0.2f, 3.0f);
			}
			else if (ConfigManager::B("Misc.EnableFOV"))
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

		const float currentTime = static_cast<float>(ImGui::GetTime());
		static float lastTime = currentTime;
		float deltaSeconds = currentTime - lastTime;
		lastTime = currentTime;
		if (deltaSeconds < 0.0f || deltaSeconds > 0.25f)
			deltaSeconds = 1.0f / 60.0f;

		const float interpSpeed = IsZoomingNow() ? 10.0f : 8.0f;
		const float alpha = std::clamp(deltaSeconds * interpSpeed, 0.0f, 1.0f);
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
			std::scoped_lock GVarsLock(gGVarsMutex);
			GVars.CameraActor = SpawnedCamera;
			return SpawnedCamera;
		}

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
				if (ConfigManager::B("Player.Freecam"))
				{
					g_OTSSmoothInitialized = false;

					if (Cam->GetAttachParentActor())
					{
						Cam->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
					}

					float FlySpeed = 20.0f;
					if (GetAsyncKeyState(VK_CONTROL) & 0x8000) FlySpeed *= 3.0f;

					SDK::FVector CamLoc = Cam->K2_GetActorLocation();
					SDK::FRotator CamRot = OakPC->GetControlRotation();
					Cam->K2_SetActorRotation(CamRot, false);

					SDK::FVector Forward = Utils::FRotatorToVector(CamRot);
					SDK::FVector Right = Utils::FRotatorToVector({ 0, CamRot.Yaw + 90.0, 0 });

					if (GetAsyncKeyState('W') & 0x8000) { CamLoc.X += Forward.X * FlySpeed; CamLoc.Y += Forward.Y * FlySpeed; CamLoc.Z += Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('S') & 0x8000) { CamLoc.X -= Forward.X * FlySpeed; CamLoc.Y -= Forward.Y * FlySpeed; CamLoc.Z -= Forward.Z * FlySpeed; }
					if (GetAsyncKeyState('D') & 0x8000) { CamLoc.X += Right.X * FlySpeed; CamLoc.Y += Right.Y * FlySpeed; CamLoc.Z += Right.Z * FlySpeed; }
					if (GetAsyncKeyState('A') & 0x8000) { CamLoc.X -= Right.X * FlySpeed; CamLoc.Y -= Right.Y * FlySpeed; CamLoc.Z -= Right.Z * FlySpeed; }
					if (GetAsyncKeyState(VK_SPACE) & 0x8000) CamLoc.Z += FlySpeed;
					if (GetAsyncKeyState(VK_SHIFT) & 0x8000) CamLoc.Z -= FlySpeed;

					Cam->K2_SetActorLocation(CamLoc, false, nullptr, false);

					if (OakPC->PlayerCameraManager->ViewTarget.target != Cam)
					{
						OakPC->SetViewTargetWithBlend(Cam, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
				else
				{
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

					if (Cam->GetAttachParentActor())
					{
						Cam->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
					}

					// Minimal spring-arm-like smoothing (stable and stateless enough)
					const SDK::FRotator ControlRot = OakPC->GetControlRotation();
					const SDK::FVector CharLoc = OakChar->K2_GetActorLocation();
					const SDK::FVector Forward = Utils::FRotatorToVector(ControlRot);
					const SDK::FVector Right = Utils::FRotatorToVector({ 0.0, ControlRot.Yaw + 90.0, 0.0 });

					SDK::FVector DesiredLoc{};
					DesiredLoc.X = CharLoc.X + (Forward.X * ConfigManager::F("Misc.OTS_X")) + (Right.X * ConfigManager::F("Misc.OTS_Y"));
					DesiredLoc.Y = CharLoc.Y + (Forward.Y * ConfigManager::F("Misc.OTS_X")) + (Right.Y * ConfigManager::F("Misc.OTS_Y"));
					DesiredLoc.Z = CharLoc.Z + 65.0 + ConfigManager::F("Misc.OTS_Z") + (Forward.Z * ConfigManager::F("Misc.OTS_X")) + (Right.Z * ConfigManager::F("Misc.OTS_Y"));

					if (!g_OTSSmoothInitialized)
					{
						g_OTSSmoothedLoc = DesiredLoc;
						g_OTSSmoothInitialized = true;
					}
					else
					{
						const float alpha = 0.28f;
						g_OTSSmoothedLoc.X += (DesiredLoc.X - g_OTSSmoothedLoc.X) * alpha;
						g_OTSSmoothedLoc.Y += (DesiredLoc.Y - g_OTSSmoothedLoc.Y) * alpha;
						g_OTSSmoothedLoc.Z += (DesiredLoc.Z - g_OTSSmoothedLoc.Z) * alpha;
					}

					Cam->K2_SetActorLocation(g_OTSSmoothedLoc, false, nullptr, false);
					Cam->K2_SetActorRotation(ControlRot, false);

					if (OakPC->PlayerCameraManager->ViewTarget.target != Cam)
					{
						OakPC->SetViewTargetWithBlend(Cam, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
					}
				}
			}
		}
		else
		{
			g_OTSSmoothInitialized = false;
			g_SmoothedShadowFOV = -1.0f;

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
