#include "pch.h"

namespace
{
	enum class EShadowCameraMode : uint8
	{
		None,
		Freecam
	};

	EShadowCameraMode g_ShadowCameraMode = EShadowCameraMode::None;
	SDK::USpringArmComponent* g_ShadowSpringArm = nullptr;
	SDK::AOakPlayerController* g_ShadowOwnerController = nullptr;
	SDK::APlayerCameraManager* g_ShadowOwnerCameraManager = nullptr;
	SDK::AActor* g_ShadowFallbackViewTarget = nullptr;
	float g_SmoothedShadowFOV = -1.0f;
	float g_SmoothedOTSADSBlend = 0.0f;
	bool g_FreecamRotationInitialized = false;
	SDK::FRotator g_FreecamRotation{};
	bool g_RequestedThirdPersonMode = false;
	bool g_RequestedFirstPersonMode = false;
	bool g_BlockShadowCameraUntilGameplayReady = false;
	float g_NativeOTSBlendAlpha = 0.0f;

	struct FPendingShadowCameraRelease
	{
		SDK::ACameraActor* Camera = nullptr;
		SDK::AOakPlayerController* Controller = nullptr;
		SDK::AActor* FallbackViewTarget = nullptr;
		SDK::APlayerCameraManager* CameraManager = nullptr;
		bool bDestroyActor = true;
		bool bRestoreViewTarget = true;
	};

	FPendingShadowCameraRelease g_PendingShadowCameraRelease{};

	constexpr double kOTSMaxWorldCoord = 2000000.0;
	constexpr float kFreecamMouseSensitivity = 0.12f;

	struct FOTSState
	{
		float OffsetX;
		float OffsetY;
		float OffsetZ;
		float FOV;
	};

	struct FNativeOTSState
	{
		bool bShouldApply = false;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
		float OffsetZ = 0.0f;
		float TargetFOV = 90.0f;
		double ProjectionX = 0.0;
		double ProjectionY = 0.0;
	};

	struct FNativeCameraPose
	{
		SDK::FVector Location{};
		SDK::FRotator Rotation{};
		float FOV = 90.0f;
	};

	bool IsValidShadowCamera(SDK::ACameraActor* CameraActor);
	bool CanAccessShadowCameraObject(SDK::ACameraActor* CameraActor);
	bool IsGameplayReadyForShadowCamera();
	bool IsNativeCameraContextUsable(uintptr_t cameraContext);
	float GetShadowCameraBaseFOV();
	FOTSState GetBlendedOTSState(float blendAlpha);
	void QueueShadowCameraRelease(bool bDestroyActor, bool bRestoreViewTarget);
	void FlushPendingShadowCameraRelease();

	constexpr uintptr_t kCurrentCacheLocOffset = 1088;
	constexpr uintptr_t kCurrentCacheRotOffset = 1112;
	constexpr uintptr_t kCurrentCacheFovOffset = 1136;
	constexpr uintptr_t kLastFrameCacheLocOffset = 12032;
	constexpr uintptr_t kLastFrameCacheRotOffset = 12056;
	constexpr uintptr_t kLastFrameCacheFovOffset = 12080;
	constexpr uintptr_t kViewTargetLocOffset = 14640;
	constexpr uintptr_t kViewTargetRotOffset = 14664;
	constexpr uintptr_t kViewTargetFovOffset = 14688;
	using NativeCameraUpdateFn = char(__fastcall*)(__int64, __int64*, float);
	constexpr SignatureRegistry::Signature kNativeCameraUpdateSignature{
		"NativeCameraUpdate",
		"41 57 41 56 41 54 56 57 53 48 81 EC ? ? ? ? "
		"66 44 0F 29 BC 24 ? ? ? ? 66 44 0F 29 B4 24 ? ? ? ? "
		"66 44 0F 29 AC 24 ? ? ? ? 66 44 0F 29 A4 24 ? ? ? ? "
		"66 44 0F 29 9C 24 ? ? ? ? 66 44 0F 29 94 24 ? ? ? ? "
		"66 44 0F 29 8C 24 ? ? ? ? 66 44 0F 29 84 24 ? ? ? ? "
		"66 0F 29 BC 24 ? ? ? ? 0F 29 B4 24 ? ? ? ? 66 0F 28 F2",
		SignatureRegistry::HookTiming::InGameReady
	};
	constexpr size_t kNativeCameraUpdateHookLen = 19;
	NativeCameraUpdateFn oNativeCameraUpdate = nullptr;

	void DescribeNativeBehaviorList(uintptr_t modePtr, char* buffer, size_t bufferSize)
	{
		if (!buffer || bufferSize == 0)
			return;

		if (!modePtr)
		{
			_snprintf_s(buffer, bufferSize, _TRUNCATE, "Mode=null");
			return;
		}

		uintptr_t behaviorsPtr = 0;
		int32_t behaviorCount = 0;
		if (!NativeInterop::ReadPointerNoexcept(modePtr + 48, behaviorsPtr) ||
			!NativeInterop::ReadInt32Noexcept(modePtr + 56, behaviorCount) ||
			behaviorCount <= 0 ||
			!behaviorsPtr)
		{
			_snprintf_s(buffer, bufferSize, _TRUNCATE, "Behaviors=0");
			return;
		}

		size_t offset = 0;
		int written = _snprintf_s(buffer, bufferSize, _TRUNCATE, "Behaviors=%d [", behaviorCount);
		if (written < 0)
			return;

		offset = static_cast<size_t>(written);
		const int32_t limit = (std::min)(behaviorCount, 10);
		for (int32_t i = 0; i < limit; ++i)
		{
			uintptr_t behaviorObj = 0;
			const char* separator = (i != 0) ? ", " : "";
			if (!NativeInterop::ReadPointerNoexcept(behaviorsPtr + (static_cast<uintptr_t>(i) * 16), behaviorObj) || !behaviorObj)
			{
				written = _snprintf_s(buffer + offset, (bufferSize > offset) ? (bufferSize - offset) : 0, _TRUNCATE, "%snull", separator);
				if (written < 0)
					return;
				offset += static_cast<size_t>(written);
				continue;
			}

			written = _snprintf_s(
				buffer + offset,
				(bufferSize > offset) ? (bufferSize - offset) : 0,
				_TRUNCATE,
				"%s0x%llX",
				separator,
				static_cast<unsigned long long>(behaviorObj));
			if (written < 0)
				return;
			offset += static_cast<size_t>(written);
		}

		if (behaviorCount > limit)
		{
			written = _snprintf_s(buffer + offset, (bufferSize > offset) ? (bufferSize - offset) : 0, _TRUNCATE, ", ...");
			if (written < 0)
				return;
			offset += static_cast<size_t>(written);
		}

		_snprintf_s(buffer + offset, (bufferSize > offset) ? (bufferSize - offset) : 0, _TRUNCATE, "]");
	}

	void LogNativeCameraState(const char* phase, __int64 cameraContext)
	{
		if (!Cheats::ShouldTraceNativeCamera())
			return;

		const uintptr_t context = static_cast<uintptr_t>(cameraContext);
		if (!IsNativeCameraContextUsable(context))
			return;

		uintptr_t modePtr = 0;
		NativeInterop::ReadPointerNoexcept(context + 14920, modePtr);

		double locX = 0.0, locY = 0.0, locZ = 0.0;
		double rotPitch = 0.0, rotYaw = 0.0, rotRoll = 0.0;
		float fov = 0.0f;
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetLocOffset, locX);
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetLocOffset + 8, locY);
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetLocOffset + 16, locZ);
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetRotOffset, rotPitch);
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetRotOffset + 8, rotYaw);
		NativeInterop::ReadDoubleNoexcept(context + kViewTargetRotOffset + 16, rotRoll);
		NativeInterop::ReadFloatNoexcept(context + kViewTargetFovOffset, fov);

		char behaviorDesc[512]{};
		DescribeNativeBehaviorList(modePtr, behaviorDesc, sizeof(behaviorDesc));
		const char* category = (std::strcmp(phase, "PreUpdate") == 0) ? "CamNativePre" : "CamNativePost";
		Logger::LogThrottled(
			Logger::Level::Debug,
			category,
			5000,
			"%s Ctx=%p Mode=%p Loc=(%.2f, %.2f, %.2f) Rot=(%.2f, %.2f, %.2f) FOV=%.2f %s",
			phase,
			reinterpret_cast<void*>(cameraContext),
			reinterpret_cast<void*>(modePtr),
			locX, locY, locZ,
			rotPitch, rotYaw, rotRoll,
			fov,
			behaviorDesc);
	}

	bool IsNativeCameraContextUsable(uintptr_t cameraContext)
	{
		if (cameraContext < 0x10000)
			return false;

		double locX = 0.0;
		double locY = 0.0;
		double locZ = 0.0;
		float fov = 0.0f;
		return NativeInterop::ReadDoubleNoexcept(cameraContext + kViewTargetLocOffset, locX) &&
			NativeInterop::ReadDoubleNoexcept(cameraContext + kViewTargetLocOffset + 8, locY) &&
			NativeInterop::ReadDoubleNoexcept(cameraContext + kViewTargetLocOffset + 16, locZ) &&
			NativeInterop::ReadFloatNoexcept(cameraContext + kViewTargetFovOffset, fov) &&
			std::isfinite(locX) &&
			std::isfinite(locY) &&
			std::isfinite(locZ) &&
			std::isfinite(fov);
	}

	char __fastcall hkNativeCameraUpdate(__int64 a1, __int64* a2, float a3)
	{
		const uintptr_t cameraContext = static_cast<uintptr_t>(a1);
		const bool bNativeCameraSafe =
			!g_BlockShadowCameraUntilGameplayReady &&
			!Utils::bIsLoading &&
			Utils::bIsInGame &&
			IsGameplayReadyForShadowCamera() &&
			IsNativeCameraContextUsable(cameraContext);

		if (!bNativeCameraSafe)
		{
			return oNativeCameraUpdate ? oNativeCameraUpdate(a1, a2, a3) : 0;
		}

		const bool bShouldLog = false;
		if (bShouldLog)
		{
			LogNativeCameraState("PreUpdate", a1);
		}

		const char result = oNativeCameraUpdate ? oNativeCameraUpdate(a1, a2, a3) : 0;
		Cheats::ApplyNativeCameraPostUpdate(cameraContext, a3);
		if (bShouldLog)
		{
			LogNativeCameraState("PostUpdate", a1);
		}

		return result;
	}

	bool ReadNativeCameraPose(uintptr_t base, uintptr_t locOffset, uintptr_t rotOffset, uintptr_t fovOffset, FNativeCameraPose& outPose)
	{
		double locX = 0.0;
		double locY = 0.0;
		double locZ = 0.0;
		double rotPitch = 0.0;
		double rotYaw = 0.0;
		double rotRoll = 0.0;
		float fov = 0.0f;
		if (!NativeInterop::ReadDoubleNoexcept(base + locOffset, locX) ||
			!NativeInterop::ReadDoubleNoexcept(base + locOffset + 8, locY) ||
			!NativeInterop::ReadDoubleNoexcept(base + locOffset + 16, locZ) ||
			!NativeInterop::ReadDoubleNoexcept(base + rotOffset, rotPitch) ||
			!NativeInterop::ReadDoubleNoexcept(base + rotOffset + 8, rotYaw) ||
			!NativeInterop::ReadDoubleNoexcept(base + rotOffset + 16, rotRoll) ||
			!NativeInterop::ReadFloatNoexcept(base + fovOffset, fov))
		{
			return false;
		}

		outPose.Location = { locX, locY, locZ };
		outPose.Rotation = { rotPitch, rotYaw, rotRoll };
		outPose.FOV = fov;
		return true;
	}

	bool WriteNativeCameraPose(uintptr_t base, uintptr_t locOffset, uintptr_t rotOffset, uintptr_t fovOffset, const FNativeCameraPose& pose)
	{
		return NativeInterop::WriteDoubleNoexcept(base + locOffset, pose.Location.X) &&
			NativeInterop::WriteDoubleNoexcept(base + locOffset + 8, pose.Location.Y) &&
			NativeInterop::WriteDoubleNoexcept(base + locOffset + 16, pose.Location.Z) &&
			NativeInterop::WriteDoubleNoexcept(base + rotOffset, pose.Rotation.Pitch) &&
			NativeInterop::WriteDoubleNoexcept(base + rotOffset + 8, pose.Rotation.Yaw) &&
			NativeInterop::WriteDoubleNoexcept(base + rotOffset + 16, pose.Rotation.Roll) &&
			NativeInterop::WriteFloatNoexcept(base + fovOffset, pose.FOV);
	}

	SDK::FVector MakeNativeCameraOffset(const FNativeOTSState& desiredState, const SDK::FRotator& rotation)
	{
		const SDK::FVector forward = Utils::FRotatorToVector(rotation);
		const SDK::FVector right = Utils::FRotatorToVector({ 0.0, rotation.Yaw + 90.0, 0.0 });
		const SDK::FVector up = { 0.0, 0.0, 1.0 };

		return {
			(forward.X * desiredState.OffsetX) + (right.X * desiredState.OffsetY) + (up.X * desiredState.OffsetZ),
			(forward.Y * desiredState.OffsetX) + (right.Y * desiredState.OffsetY) + (up.Y * desiredState.OffsetZ),
			(forward.Z * desiredState.OffsetX) + (right.Z * desiredState.OffsetY) + (up.Z * desiredState.OffsetZ)
		};
	}

	void ApplyNativeCameraOffsetToPose(FNativeCameraPose& pose, const FNativeOTSState& desiredState)
	{
		const SDK::FVector offset = MakeNativeCameraOffset(desiredState, pose.Rotation);
		pose.Location.X += offset.X;
		pose.Location.Y += offset.Y;
		pose.Location.Z += offset.Z;
		pose.FOV = std::clamp(desiredState.TargetFOV, 20.0f, 180.0f);
	}

	void MirrorNativeCameraPoseToSdk(const FNativeCameraPose& currentPose)
	{
		if (!GVars.PlayerController || !GVars.PlayerController->PlayerCameraManager)
			return;

		auto* cameraManager = GVars.PlayerController->PlayerCameraManager;
		cameraManager->ViewTarget.POV.Location = currentPose.Location;
		cameraManager->ViewTarget.POV.Rotation = currentPose.Rotation;
		cameraManager->ViewTarget.POV.fov = currentPose.FOV;
		cameraManager->CameraCachePrivate.POV.Location = currentPose.Location;
		cameraManager->CameraCachePrivate.POV.Rotation = currentPose.Rotation;
		cameraManager->CameraCachePrivate.POV.fov = currentPose.FOV;

		if (cameraManager->PendingViewTarget.target == cameraManager->ViewTarget.target)
		{
			cameraManager->PendingViewTarget.POV.Location = currentPose.Location;
			cameraManager->PendingViewTarget.POV.Rotation = currentPose.Rotation;
			cameraManager->PendingViewTarget.POV.fov = currentPose.FOV;
		}

		if (GVars.POV)
		{
			GVars.POV->Location = currentPose.Location;
			GVars.POV->Rotation = currentPose.Rotation;
			GVars.POV->fov = currentPose.FOV;
		}
	}


	bool ShouldReleaseShadowCameraForWorldLifecycle()
	{
		SDK::ACameraActor* shadowCamera = nullptr;
		SDK::APlayerController* playerController = nullptr;
		SDK::ACharacter* character = nullptr;
		SDK::UWorld* world = nullptr;
		{
			std::scoped_lock GVarsLock(gGVarsMutex);
			shadowCamera = GVars.CameraActor;
			playerController = GVars.PlayerController;
			character = GVars.Character;
			world = GVars.World;
		}

		if (shadowCamera && (!playerController || !world || !character))
		{
			return true;
		}
		return false;
	}

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

	void ResetShadowCameraBindings()
	{
		g_ShadowOwnerController = nullptr;
		g_ShadowOwnerCameraManager = nullptr;
		g_ShadowFallbackViewTarget = nullptr;
	}

	void ResetShadowCameraState()
	{
		ResetShadowSpringArm();
		g_SmoothedShadowFOV = -1.0f;
		g_SmoothedOTSADSBlend = 0.0f;
		g_FreecamRotationInitialized = false;
		g_RequestedThirdPersonMode = false;
		g_RequestedFirstPersonMode = false;
	}

	bool IsGameplayReadyForShadowCamera()
	{
		return Utils::bIsInGame &&
			GVars.World != nullptr &&
			GVars.PlayerController != nullptr &&
			GVars.Character != nullptr;
	}

	bool ShouldForceTravelShadowCameraShutdown(const SDK::UObject* Object, SDK::UFunction* Function)
	{
		if (!Object || !Function)
			return false;

		const std::string functionName = Function->GetName();
		if (functionName == "ClientTravel" ||
			functionName == "ClientTravelInternal" ||
			functionName == "LocalTravel" ||
			functionName == "ClientReturnToMainMenuWithTextReason" ||
			functionName == "ReturnToMainMenuHost" ||
			functionName == "OpenLevel" ||
			functionName == "OpenLevelBySoftObjectPtr" ||
			functionName == "OnLeaveGameChoiceMade")
		{
			return true;
		}

		if (functionName.find("Travel") != std::string::npos ||
			functionName.find("MainMenu") != std::string::npos ||
			functionName.find("LeaveGame") != std::string::npos ||
			functionName.find("Disconnect") != std::string::npos)
		{
			const std::string className = Object->Class ? Object->Class->GetName() : "";
			return className.find("PlayerController") != std::string::npos ||
				className.find("GameMode") != std::string::npos ||
				className.find("GameInstance") != std::string::npos ||
				className.find("UIScript") != std::string::npos ||
				className.find("TravelNotification") != std::string::npos ||
				className.find("DispatchEvents") != std::string::npos;
		}

		return false;
	}

	static void DestroyShadowCameraSafely(SDK::ACameraActor* cam, bool bDestroyActor)
	{
		__try
		{
			if (cam->owner)
			{
				cam->SetOwner(nullptr);
			}

			if (cam->GetAttachParentActor())
			{
				cam->K2_DetachFromActor(SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld, SDK::EDetachmentRule::KeepWorld);
			}

			if (bDestroyActor && !cam->bActorIsBeingDestroyed)
			{
				cam->GbxDestroyActor(true);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	void QueueShadowCameraRelease(bool bDestroyActor, bool bRestoreViewTarget)
	{
		SDK::ACameraActor* cam = nullptr;
		SDK::AOakPlayerController* oakPc = nullptr;
		SDK::AOakCharacter* oakChar = nullptr;
		SDK::AActor* fallbackViewTarget = nullptr;
		SDK::APlayerCameraManager* cachedCameraManager = nullptr;
		{
			std::scoped_lock GVarsLock(gGVarsMutex);
			cam = GVars.CameraActor;
			GVars.CameraActor = nullptr;
			if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
				oakPc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
			if (GVars.Character && GVars.Character->IsA(SDK::AOakCharacter::StaticClass()))
				oakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
			if (oakChar && Utils::IsValidActor(oakChar))
				fallbackViewTarget = oakChar;
			else if (GVars.Pawn && Utils::IsValidActor(GVars.Pawn))
				fallbackViewTarget = GVars.Pawn;
			else if (GVars.PlayerController && GVars.PlayerController->Pawn && Utils::IsValidActor(GVars.PlayerController->Pawn))
				fallbackViewTarget = GVars.PlayerController->Pawn;
		}

		if (!oakPc && g_ShadowOwnerController && Utils::IsValidActor(g_ShadowOwnerController))
			oakPc = g_ShadowOwnerController;
		if (!fallbackViewTarget && g_ShadowFallbackViewTarget && Utils::IsValidActor(g_ShadowFallbackViewTarget))
			fallbackViewTarget = g_ShadowFallbackViewTarget;
		if (g_ShadowOwnerCameraManager && Utils::IsValidActor(g_ShadowOwnerCameraManager))
			cachedCameraManager = g_ShadowOwnerCameraManager;

		ResetShadowCameraState();

		g_PendingShadowCameraRelease.Camera = cam;
		g_PendingShadowCameraRelease.Controller = oakPc;
		g_PendingShadowCameraRelease.FallbackViewTarget = fallbackViewTarget;
		g_PendingShadowCameraRelease.CameraManager = cachedCameraManager;
		g_PendingShadowCameraRelease.bDestroyActor = bDestroyActor;
		g_PendingShadowCameraRelease.bRestoreViewTarget = bRestoreViewTarget;

		ResetShadowCameraBindings();
	}

	void FlushPendingShadowCameraRelease()
	{
		SDK::ACameraActor* cam = g_PendingShadowCameraRelease.Camera;
		SDK::AOakPlayerController* oakPc = g_PendingShadowCameraRelease.Controller;
		SDK::AActor* fallbackViewTarget = g_PendingShadowCameraRelease.FallbackViewTarget;
		SDK::APlayerCameraManager* cachedCameraManager = g_PendingShadowCameraRelease.CameraManager;
		const bool bDestroyActor = g_PendingShadowCameraRelease.bDestroyActor;
		const bool bRestoreViewTarget = g_PendingShadowCameraRelease.bRestoreViewTarget;

		g_PendingShadowCameraRelease = {};

		if (!CanAccessShadowCameraObject(cam))
		{
			return;
		}

		SDK::APlayerCameraManager* cameraManager = cachedCameraManager;
		if (oakPc && oakPc->PlayerCameraManager && Utils::IsValidActor(oakPc->PlayerCameraManager))
			cameraManager = oakPc->PlayerCameraManager;

		if (bRestoreViewTarget && cameraManager)
		{
			if (cameraManager->ViewTarget.target == cam)
			{
				if (fallbackViewTarget && oakPc && Utils::IsValidActor(oakPc))
				{
					oakPc->SetViewTargetWithBlend(fallbackViewTarget, 0.0f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
				}
				else
				{
					cameraManager->ViewTarget.target = fallbackViewTarget;
				}
			}

			if (cameraManager->PendingViewTarget.target == cam)
			{
				cameraManager->PendingViewTarget.target = fallbackViewTarget;
			}

			if (cameraManager->AnimCameraActor == cam)
			{
				cameraManager->AnimCameraActor = nullptr;
			}
		}

		if (oakPc)
		{
			if (oakPc->NetConnection && oakPc->NetConnection->ViewTarget == cam)
			{
				oakPc->NetConnection->ViewTarget = fallbackViewTarget;
			}

			if (oakPc->PendingSwapConnection && oakPc->PendingSwapConnection->ViewTarget == cam)
			{
				oakPc->PendingSwapConnection->ViewTarget = fallbackViewTarget;
			}
		}

		DestroyShadowCameraSafely(cam, bDestroyActor);
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

	bool IsZoomingNow()
	{
		if (!GVars.Character || !GVars.Character->IsA(SDK::AOakCharacter::StaticClass())) return false;
		const SDK::AOakCharacter* oakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		const SDK::EZoomState zoomState = oakChar->ZoomState.State;
		return zoomState == SDK::EZoomState::ZoomingIn || zoomState == SDK::EZoomState::Zoomed;
	}

	bool IsNativeOTSGameplayReady()
	{
		if (!ConfigManager::B("Player.OverShoulder") ||
			ConfigManager::B("Player.Freecam") ||
			!Utils::bIsInGame ||
			!GVars.World ||
			!GVars.PlayerController ||
			!GVars.PlayerController->PlayerCameraManager ||
			!GVars.Character ||
			!GVars.Character->IsA(SDK::AOakCharacter::StaticClass()))
		{
			return false;
		}

		if (GVars.PlayerController->PlayerCameraManager->ViewTarget.target != GVars.Character)
		{
			return false;
		}

		const auto* oakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		const std::string modeStr = SDK::APlayerCameraModeManager::GetActorCameraMode(const_cast<SDK::AOakCharacter*>(oakChar)).ToString();
		if (modeStr.find("FirstPerson") != std::string::npos && !IsZoomingNow())
		{
			return false;
		}

		return true;
	}

	float GetShadowCameraBaseFOV()
	{
		if (ConfigManager::B("Misc.EnableFOV"))
		{
			return std::clamp(ConfigManager::F("Misc.FOV"), 20.0f, 180.0f);
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

	float UpdateNativeOTSBlendAlpha(float deltaSeconds)
	{
		(void)deltaSeconds;
		const bool bShouldZoom = IsZoomingNow() && ConfigManager::B("Misc.OTSADSOverride");
		const float duration = std::clamp(ConfigManager::F("Misc.OTSADSBlendTime"), 0.01f, 2.0f);
		const float targetBlend = bShouldZoom ? 1.0f : 0.0f;
		const float alpha = std::clamp(GetFrameDeltaSeconds() / duration, 0.0f, 1.0f);
		g_NativeOTSBlendAlpha += (targetBlend - g_NativeOTSBlendAlpha) * alpha;
		return g_NativeOTSBlendAlpha;
	}

	FNativeOTSState BuildDesiredNativeOTSState(float deltaSeconds)
	{
		FNativeOTSState state{};
		if (!IsNativeOTSGameplayReady())
		{
			g_NativeOTSBlendAlpha = 0.0f;
			return state;
		}

		const float blendAlpha = UpdateNativeOTSBlendAlpha(deltaSeconds);
		const FOTSState blendedState = GetBlendedOTSState(blendAlpha);
		const float baseFOV = GetShadowCameraBaseFOV();

		state.bShouldApply = true;
		state.OffsetX = blendedState.OffsetX;
		state.OffsetY = blendedState.OffsetY;
		state.OffsetZ = blendedState.OffsetZ;
		state.TargetFOV = ConfigManager::B("Misc.OTSADSOverride")
			? blendedState.FOV
			: baseFOV;
		// Apply an off-center projection so the character is framed off-center
		// instead of feeling like a normal centered third-person camera with a small translation.
		state.ProjectionX = 0.0;
		state.ProjectionY = 0.0;
		return state;
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

	float GetNativeOTSResetFOV()
	{
		if (ConfigManager::B("Player.OverShoulder"))
		{
			return std::clamp(GetBlendedOTSState(0.0f).FOV, 20.0f, 180.0f);
		}

		if (ConfigManager::B("Misc.EnableFOV"))
		{
			return GetAppliedFOV(false);
		}

		return std::clamp(GetShadowCameraBaseFOV(), 20.0f, 180.0f);
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

	void ApplyRuntimeCameraFOV(float targetFOV)
	{
		if (!GVars.PlayerController) return;
		GVars.PlayerController->fov(targetFOV);

		if (!GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
			return;

		SDK::AOakPlayerController* oakPc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		if (!oakPc || !oakPc->PlayerCameraManager) return;

		oakPc->PlayerCameraManager->ViewTarget.POV.fov = targetFOV;
		oakPc->PlayerCameraManager->PendingViewTarget.POV.fov = targetFOV;
		oakPc->PlayerCameraManager->CameraCachePrivate.POV.fov = targetFOV;
		oakPc->PlayerCameraManager->LastFrameCameraCachePrivate.POV.fov = targetFOV;
		if (GVars.POV)
		{
			GVars.POV->fov = targetFOV;
		}
	}

	void ApplyConfiguredBaseFOV(float targetFOV)
	{
		if (!GVars.PlayerController) return;
		ApplyRuntimeCameraFOV(targetFOV);

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

	bool CanAccessShadowCameraObject(SDK::ACameraActor* CameraActor)
	{
		if (!CameraActor) return false;

		__try
		{
			if (IsBadReadPtr(CameraActor, sizeof(void*)) || !CameraActor->VTable) return false;
			if (!CameraActor->Class || IsBadReadPtr(CameraActor->Class, sizeof(void*))) return false;
			return CameraActor->IsA(SDK::ACameraActor::StaticClass());
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
		if (OakPC && Utils::IsValidActor(OakPC))
		{
			g_ShadowOwnerController = OakPC;
			if (OakPC->PlayerCameraManager && Utils::IsValidActor(OakPC->PlayerCameraManager))
				g_ShadowOwnerCameraManager = OakPC->PlayerCameraManager;
			if (OakPC->Pawn && Utils::IsValidActor(OakPC->Pawn))
				g_ShadowFallbackViewTarget = OakPC->Pawn;
		}
		if (OakChar && Utils::IsValidActor(OakChar))
			g_ShadowFallbackViewTarget = OakChar;

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
		if (!GVars.PlayerController || !GVars.Character || !GVars.World)
		{
			QueueShadowCameraRelease(true, true);
			return;
		}
		SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		if (!OakPC || !OakChar || !OakPC->PlayerCameraManager)
		{
			QueueShadowCameraRelease(true, true);
			return;
		}
		if (!Utils::IsValidActor(OakPC) || !Utils::IsValidActor(OakChar))
		{
			QueueShadowCameraRelease(true, true);
			return;
		}

		static float LastTransitionTime = 0.0f;
		const float CurrentTime = (float)ImGui::GetTime();
		const bool bIsZooming = ((uint8)OakChar->ZoomState.State != 0);
		const bool bUseFreecam = ConfigManager::B("Player.Freecam");
		const bool bUseOverShoulder = ConfigManager::B("Player.OverShoulder");

		if (bUseFreecam)
		{
			SDK::ACameraActor* Cam = EnsureShadowCamera(OakPC, OakChar);
			if (Cam)
			{
				SDK::USpringArmComponent* SpringArm = EnsureShadowSpringArm(Cam);
				if (SpringArm && g_ShadowCameraMode != EShadowCameraMode::Freecam)
				{
					CollapseSpringArmForFreecam(Cam, SpringArm);
				}
				g_SmoothedOTSADSBlend = 0.0f;
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
					OakPC->SetViewTargetWithBlend(Cam, 0.0f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
				}
			}
		}
		else
		{
			QueueShadowCameraRelease(true, true);

			bool bShouldBeInThirdPerson = ConfigManager::B("Player.ThirdPerson") || bUseOverShoulder;
			if (ConfigManager::B("Player.ThirdPerson") && !bUseOverShoulder && bIsZooming && ConfigManager::B("Misc.ThirdPersonADSFirstPerson"))
			{
				bShouldBeInThirdPerson = false;
			}

			if (bUseOverShoulder)
			{
				GetCurrentOTSADSBlend(bIsZooming);
			}
			else
			{
				g_SmoothedOTSADSBlend = 0.0f;
			}

			if (OakPC->PlayerCameraManager->ViewTarget.target != OakChar)
			{
				OakPC->SetViewTargetWithBlend(OakChar, 0.15f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
			}

				const std::string ModeStr = SDK::APlayerCameraModeManager::GetActorCameraMode(OakChar).ToString();
				if (bShouldBeInThirdPerson)
				{
					if (ModeStr.find("ThirdPerson") != std::string::npos)
					{
						g_RequestedThirdPersonMode = false;
					}
				else if (!g_RequestedThirdPersonMode && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(
						SDK::UKismetStringLibrary::Conv_StringToName(L"ThirdPerson"),
						SDK::UKismetStringLibrary::Conv_StringToName(L"Default"),
						0.15f,
						false,
						false);
					LastTransitionTime = CurrentTime;
					g_RequestedThirdPersonMode = true;
				}
				g_RequestedFirstPersonMode = false;
			}
			else
			{
				if (ModeStr.find("ThirdPerson") == std::string::npos)
				{
					g_RequestedFirstPersonMode = false;
				}
				else if (!g_RequestedFirstPersonMode && (CurrentTime - LastTransitionTime > 0.5f))
				{
					OakPC->CameraTransition(
						SDK::UKismetStringLibrary::Conv_StringToName(L"FirstPerson"),
						SDK::UKismetStringLibrary::Conv_StringToName(L"Default"),
						0.15f,
						false,
						false);
					LastTransitionTime = CurrentTime;
					g_RequestedFirstPersonMode = true;
				}
				g_RequestedThirdPersonMode = false;
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
	if (ConfigManager::B("Player.OverShoulder")) return;

	static float LastFOV = -1.0f;
	if (ConfigManager::B("Misc.EnableFOV"))
	{
		const float targetFOV = GetAppliedFOV(false);
		// ADS often overrides FOV every frame, so force-apply while zooming.
		if (LastFOV != targetFOV || IsZoomingNow())
		{
			ApplyConfiguredBaseFOV(targetFOV);
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
	SignatureRegistry::EnsureHook(
		kNativeCameraUpdateSignature,
		reinterpret_cast<void*>(&hkNativeCameraUpdate),
		reinterpret_cast<void**>(&oNativeCameraUpdate),
		kNativeCameraUpdateHookLen,
		"CamNative",
		true);

	if (g_BlockShadowCameraUntilGameplayReady)
	{
		if (!IsGameplayReadyForShadowCamera())
		{
			QueueShadowCameraRelease(true, true);
			return;
		}

		Logger::LogThrottled(Logger::Level::Debug, "Camera", 2000, "Gameplay became ready again, allowing shadow camera recreation");
		g_BlockShadowCameraUntilGameplayReady = false;
	}

	if (ShouldReleaseShadowCameraForWorldLifecycle())
	{
		Logger::LogThrottled(Logger::Level::Debug, "Camera", 1000, "Releasing shadow camera on engine world transition state");
		QueueShadowCameraRelease(true, true);
	}

	if (Utils::bIsLoading || !Utils::bIsInGame)
	{
		QueueShadowCameraRelease(true, true);
		return;
	}

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

	if (ConfigManager::B("Misc.EnableFOV") && !ConfigManager::B("Player.OverShoulder"))
	{
		const float appliedFOV = GetAppliedFOV(false);
		ApplyConfiguredBaseFOV(appliedFOV);
	}
	ApplyViewModelFOV();

	Cheats::ChangeGameRenderSettings();
}

void Cheats::ShutdownCamera()
{
	QueueShadowCameraRelease(true, true);
}

bool Cheats::ShouldTraceNativeCamera()
{
#if BL4_DEBUG_BUILD
	return ConfigManager::B("Misc.Debug") &&
		!g_BlockShadowCameraUntilGameplayReady &&
		!Utils::bIsLoading &&
		Utils::bIsInGame &&
		IsNativeOTSGameplayReady();
#else
	return false;
#endif
}

void Cheats::ApplyNativeCameraPostUpdate(uintptr_t cameraContext, float deltaSeconds)
{
	if (!cameraContext ||
		g_BlockShadowCameraUntilGameplayReady ||
		Utils::bIsLoading ||
		!Utils::bIsInGame ||
		!IsGameplayReadyForShadowCamera() ||
		!IsNativeCameraContextUsable(cameraContext))
	{
		return;
	}

	FNativeCameraPose currentViewTargetPose{};
	if (!ReadNativeCameraPose(cameraContext, kViewTargetLocOffset, kViewTargetRotOffset, kViewTargetFovOffset, currentViewTargetPose))
	{
		return;
	}

	const FNativeOTSState desiredState = BuildDesiredNativeOTSState(deltaSeconds);
	if (desiredState.bShouldApply)
	{
		FNativeCameraPose adjustedViewTargetPose = currentViewTargetPose;
		ApplyNativeCameraOffsetToPose(adjustedViewTargetPose, desiredState);
		WriteNativeCameraPose(cameraContext, kViewTargetLocOffset, kViewTargetRotOffset, kViewTargetFovOffset, adjustedViewTargetPose);

		FNativeCameraPose currentCachePose{};
		if (ReadNativeCameraPose(cameraContext, kCurrentCacheLocOffset, kCurrentCacheRotOffset, kCurrentCacheFovOffset, currentCachePose))
		{
			ApplyNativeCameraOffsetToPose(currentCachePose, desiredState);
			WriteNativeCameraPose(cameraContext, kCurrentCacheLocOffset, kCurrentCacheRotOffset, kCurrentCacheFovOffset, currentCachePose);
		}
		else
		{
			currentCachePose = adjustedViewTargetPose;
		}

		MirrorNativeCameraPoseToSdk(currentCachePose);
		ApplyRuntimeCameraFOV(adjustedViewTargetPose.FOV);
		return;
	}

	if (ConfigManager::B("Misc.EnableFOV") && IsZoomingNow())
	{
		const float targetFOV = GetAppliedFOV(false);
		NativeInterop::WriteFloatNoexcept(cameraContext + kViewTargetFovOffset, targetFOV);
		NativeInterop::WriteFloatNoexcept(cameraContext + kCurrentCacheFovOffset, targetFOV);
		ApplyRuntimeCameraFOV(targetFOV);
		return;
	}

	const float targetFOV = GetNativeOTSResetFOV();
	NativeInterop::WriteFloatNoexcept(cameraContext + kViewTargetFovOffset, targetFOV);
	NativeInterop::WriteFloatNoexcept(cameraContext + kCurrentCacheFovOffset, targetFOV);
	ApplyRuntimeCameraFOV(targetFOV);
}

bool Cheats::HandleCameraEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	(void)Params;
	FlushPendingShadowCameraRelease();
	if (ShouldForceTravelShadowCameraShutdown(Object, Function))
	{
		if (!g_BlockShadowCameraUntilGameplayReady)
		{
			const std::string functionName = Function ? Function->GetName() : "Unknown";
			const std::string className = (Object && Object->Class) ? Object->Class->GetName() : "Unknown";
			Logger::LogThrottled(Logger::Level::Debug, "Camera", 250, "ProcessEvent detected travel/menu transition (%s::%s), blocking shadow camera", className.c_str(), functionName.c_str());
		}

		g_BlockShadowCameraUntilGameplayReady = true;
		QueueShadowCameraRelease(true, true);
		FlushPendingShadowCameraRelease();
	}

	return false;
}
