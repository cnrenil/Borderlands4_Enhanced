#include "pch.h"

namespace
{
	float g_SmoothedOTSADSBlend = 0.0f;
	bool g_FreecamRotationInitialized = false;
	SDK::FRotator g_FreecamRotation{};
	bool g_RequestedThirdPersonMode = false;
	bool g_RequestedFirstPersonMode = false;
	bool g_BlockNativeCameraUntilGameplayReady = false;
	float g_NativeOTSBlendAlpha = 0.0f;

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

	struct FNativeFreecamState
	{
		bool bInitialized = false;
		FNativeCameraPose Pose{};
	};

	bool IsGameplayReadyForNativeCamera();
	bool IsNativeCameraContextUsable(uintptr_t cameraContext);
	float GetCameraBaseFOV();
	FOTSState GetBlendedOTSState(float blendAlpha);
	float GetAppliedFOV(bool bForOTS);
	FNativeOTSState BuildDesiredNativeOTSState(float deltaSeconds);
	float GetNativeOTSResetFOV();
	bool IsZoomingNow();
	void ApplyRuntimeCameraFOV(float targetFOV);
	void RegisterNativeCameraHookSignature();
	bool IsNativeFreecamGameplayReady();
	void ResetNativeFreecamState();
	void ResetNativeCameraState();
	FNativeCameraPose UpdateNativeFreecamPose(const FNativeCameraPose& fallbackPose);
	void ApplyNativeCameraPose(uintptr_t cameraContext, const FNativeCameraPose& pose);
	void ApplyNativeCameraPostUpdateImpl(uintptr_t cameraContext, float deltaSeconds);

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
	FNativeFreecamState g_NativeFreecamState{};

	bool IsValidWorldForNativeCamera()
	{
		if (!GVars.World || !GVars.World->VTable)
			return false;

		SDK::UWorld* currentWorld = Utils::GetWorldSafe();
		return currentWorld && currentWorld == GVars.World;
	}

	bool IsValidPlayerControllerForNativeCamera()
	{
		if (!GVars.PlayerController || !Utils::IsValidActor(GVars.PlayerController))
			return false;

		SDK::APlayerController* currentPlayerController = Utils::GetPlayerController();
		return currentPlayerController && currentPlayerController == GVars.PlayerController;
	}

	bool IsValidCameraManagerForNativeCamera()
	{
		return GVars.PlayerController &&
			GVars.PlayerController->PlayerCameraManager &&
			Utils::IsValidActor(GVars.PlayerController->PlayerCameraManager);
	}

	bool IsValidCharacterForNativeCamera()
	{
		return GVars.Character &&
			Utils::IsValidActor(GVars.Character) &&
			GVars.Character->IsA(SDK::AOakCharacter::StaticClass());
	}

	void RegisterNativeCameraHookSignature()
	{
		SignatureRegistry::Register(
			kNativeCameraUpdateSignature.Name,
			kNativeCameraUpdateSignature.Pattern,
			kNativeCameraUpdateSignature.Timing);
	}

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
			!g_BlockNativeCameraUntilGameplayReady &&
			!Utils::bIsLoading &&
			Utils::bIsInGame &&
			IsGameplayReadyForNativeCamera() &&
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
		if (!IsValidPlayerControllerForNativeCamera() || !IsValidCameraManagerForNativeCamera())
			return;

		__try
		{
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

			if (GVars.POV && !IsBadReadPtr(GVars.POV, sizeof(FMinimalViewInfo)))
			{
				GVars.POV->Location = currentPose.Location;
				GVars.POV->Rotation = currentPose.Rotation;
				GVars.POV->fov = currentPose.FOV;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			g_BlockNativeCameraUntilGameplayReady = true;
			ResetNativeCameraState();
			ResetNativeFreecamState();
		}
	}

	void ApplyNativeCameraPose(uintptr_t cameraContext, const FNativeCameraPose& pose)
	{
		WriteNativeCameraPose(cameraContext, kViewTargetLocOffset, kViewTargetRotOffset, kViewTargetFovOffset, pose);
		WriteNativeCameraPose(cameraContext, kCurrentCacheLocOffset, kCurrentCacheRotOffset, kCurrentCacheFovOffset, pose);
		WriteNativeCameraPose(cameraContext, kLastFrameCacheLocOffset, kLastFrameCacheRotOffset, kLastFrameCacheFovOffset, pose);
		MirrorNativeCameraPoseToSdk(pose);
		ApplyRuntimeCameraFOV(pose.FOV);
	}

	void ApplyNativeCameraPostUpdateImpl(uintptr_t cameraContext, float deltaSeconds)
	{
		if (!cameraContext ||
			g_BlockNativeCameraUntilGameplayReady ||
			Utils::bIsLoading ||
			!Utils::bIsInGame ||
			!IsGameplayReadyForNativeCamera() ||
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
		if (IsNativeFreecamGameplayReady())
		{
			const FNativeCameraPose freecamPose = UpdateNativeFreecamPose(currentViewTargetPose);
			ApplyNativeCameraPose(cameraContext, freecamPose);
			return;
		}

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

	void ResetNativeCameraState()
	{
		g_SmoothedOTSADSBlend = 0.0f;
		g_FreecamRotationInitialized = false;
		g_RequestedThirdPersonMode = false;
		g_RequestedFirstPersonMode = false;
		g_NativeOTSBlendAlpha = 0.0f;
	}

	void ResetNativeFreecamState()
	{
		g_NativeFreecamState = {};
	}

	bool IsGameplayReadyForNativeCamera()
	{
		return Utils::bIsInGame &&
			!Utils::bIsLoading &&
			IsValidWorldForNativeCamera() &&
			IsValidPlayerControllerForNativeCamera() &&
			IsValidCameraManagerForNativeCamera() &&
			IsValidCharacterForNativeCamera();
	}

	bool IsNativeFreecamGameplayReady()
	{
		return ConfigManager::B("Player.Freecam") &&
			!g_BlockNativeCameraUntilGameplayReady &&
			!Utils::bIsLoading &&
			IsGameplayReadyForNativeCamera() &&
			GVars.PlayerController &&
			GVars.PlayerController->PlayerCameraManager;
	}

	bool ShouldForceTravelCameraReset(const SDK::UObject* Object, SDK::UFunction* Function)
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

	FNativeCameraPose UpdateNativeFreecamPose(const FNativeCameraPose& fallbackPose)
	{
		if (!g_NativeFreecamState.bInitialized)
		{
			g_NativeFreecamState.Pose = fallbackPose;
			g_NativeFreecamState.Pose.FOV = GetAppliedFOV(false);
			g_NativeFreecamState.bInitialized = true;
		}

		FNativeCameraPose& pose = g_NativeFreecamState.Pose;
		pose.Rotation = UpdateFreecamRotation(pose.Rotation);
		pose.FOV = GetAppliedFOV(false);

		float flySpeed = 20.0f;
		if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
			flySpeed *= 3.0f;

		const SDK::FVector forward = Utils::FRotatorToVector(pose.Rotation);
		const SDK::FVector right = Utils::FRotatorToVector({ 0.0, pose.Rotation.Yaw + 90.0, 0.0 });

		if (GetAsyncKeyState('W') & 0x8000) { pose.Location.X += forward.X * flySpeed; pose.Location.Y += forward.Y * flySpeed; pose.Location.Z += forward.Z * flySpeed; }
		if (GetAsyncKeyState('S') & 0x8000) { pose.Location.X -= forward.X * flySpeed; pose.Location.Y -= forward.Y * flySpeed; pose.Location.Z -= forward.Z * flySpeed; }
		if (GetAsyncKeyState('D') & 0x8000) { pose.Location.X += right.X * flySpeed; pose.Location.Y += right.Y * flySpeed; pose.Location.Z += right.Z * flySpeed; }
		if (GetAsyncKeyState('A') & 0x8000) { pose.Location.X -= right.X * flySpeed; pose.Location.Y -= right.Y * flySpeed; pose.Location.Z -= right.Z * flySpeed; }
		if (GetAsyncKeyState(VK_SPACE) & 0x8000) pose.Location.Z += flySpeed;
		if (GetAsyncKeyState(VK_SHIFT) & 0x8000) pose.Location.Z -= flySpeed;

		if (!IsReasonableWorldLocation(pose.Location))
		{
			pose.Location = fallbackPose.Location;
		}

		return pose;
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

	bool IsZoomingNow()
	{
		if (!IsValidCharacterForNativeCamera()) return false;
		const SDK::AOakCharacter* oakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		const SDK::EZoomState zoomState = oakChar->ZoomState.State;
		return zoomState == SDK::EZoomState::ZoomingIn || zoomState == SDK::EZoomState::Zoomed;
	}

	bool IsNativeOTSGameplayReady()
	{
		if (!ConfigManager::B("Player.ThirdPerson") ||
			!ConfigManager::B("Player.OverShoulder") ||
			ConfigManager::B("Player.Freecam") ||
			!IsGameplayReadyForNativeCamera())
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

	float GetCameraBaseFOV()
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

		if (ConfigManager::B("Misc.OTSADSOverride") &&
			ConfigManager::B("Misc.OTSADSFirstPerson") &&
			IsZoomingNow())
		{
			g_NativeOTSBlendAlpha = 0.0f;
			return state;
		}

		const float blendAlpha = UpdateNativeOTSBlendAlpha(deltaSeconds);
		const FOTSState blendedState = GetBlendedOTSState(blendAlpha);
		const float baseFOV = GetCameraBaseFOV();

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
			GetCameraBaseFOV()
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

	float GetAppliedFOV(bool bForOTS)
	{
		float fov = bForOTS ? GetBlendedOTSState(g_SmoothedOTSADSBlend).FOV : ConfigManager::F("Misc.FOV");
		if (IsZoomingNow())
		{
			if (!bForOTS && ConfigManager::B("Misc.EnableFOV"))
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

		return std::clamp(GetCameraBaseFOV(), 20.0f, 180.0f);
	}

	void ApplyRuntimeCameraFOV(float targetFOV)
	{
		if (!IsValidPlayerControllerForNativeCamera())
			return;

		__try
		{
			GVars.PlayerController->fov(targetFOV);

			if (!GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()) ||
				!IsValidCameraManagerForNativeCamera())
			{
				return;
			}

			SDK::AOakPlayerController* oakPc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
			oakPc->PlayerCameraManager->ViewTarget.POV.fov = targetFOV;
			oakPc->PlayerCameraManager->PendingViewTarget.POV.fov = targetFOV;
			oakPc->PlayerCameraManager->CameraCachePrivate.POV.fov = targetFOV;
			oakPc->PlayerCameraManager->LastFrameCameraCachePrivate.POV.fov = targetFOV;
			if (GVars.POV && !IsBadReadPtr(GVars.POV, sizeof(FMinimalViewInfo)))
			{
				GVars.POV->fov = targetFOV;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			g_BlockNativeCameraUntilGameplayReady = true;
			ResetNativeCameraState();
			ResetNativeFreecamState();
		}
	}

	void ApplyConfiguredBaseFOV(float targetFOV)
	{
		if (!IsValidPlayerControllerForNativeCamera() || !IsValidCameraManagerForNativeCamera()) return;
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
		if (!IsValidPlayerControllerForNativeCamera() || !IsValidCameraManagerForNativeCamera()) return;
		if (!GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass())) return;

		SDK::AOakPlayerController* pc = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		if (!pc->PlayerCameraManager) return;
		if (!pc->PlayerCameraManager->IsA(SDK::APlayerCameraModeManager::StaticClass())) return;

		SDK::APlayerCameraModeManager* cameraMode = static_cast<SDK::APlayerCameraModeManager*>(pc->PlayerCameraManager);
		if (!cameraMode || !cameraMode->CameraModeState) return;

		const float newViewModelValue = std::clamp(ConfigManager::F("Misc.ViewModelFOV"), 60.0f, 150.0f);
		cameraMode->CameraModeState->SetViewModelFOV(newViewModelValue, true);
	}

	void UpdateCameraModes()
	{
		if (!GVars.PlayerController || !GVars.Character || !GVars.World)
		{
			ResetNativeCameraState();
			ResetNativeFreecamState();
			return;
		}
		SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
		SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		if (!OakPC || !OakChar || !OakPC->PlayerCameraManager)
		{
			ResetNativeCameraState();
			ResetNativeFreecamState();
			return;
		}
		if (!Utils::IsValidActor(OakPC) || !Utils::IsValidActor(OakChar))
		{
			ResetNativeCameraState();
			ResetNativeFreecamState();
			return;
		}

		static float LastTransitionTime = 0.0f;
		const float CurrentTime = (float)ImGui::GetTime();
		const bool bIsZooming = ((uint8)OakChar->ZoomState.State != 0);
		const bool bUseFreecam = ConfigManager::B("Player.Freecam");
		const bool bUseOverShoulder = ConfigManager::B("Player.OverShoulder");
		const bool bUseThirdPerson = ConfigManager::B("Player.ThirdPerson");

		if (bUseFreecam)
		{
			g_SmoothedOTSADSBlend = 0.0f;
			if (OakPC->PlayerCameraManager->ViewTarget.target != OakChar)
			{
				OakPC->SetViewTargetWithBlend(OakChar, 0.0f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
			}
			g_RequestedFirstPersonMode = false;
			g_RequestedThirdPersonMode = false;
		}
		else
		{
			ResetNativeFreecamState();

			bool bShouldBeInThirdPerson = bUseThirdPerson;
			const bool bOTSAdsFirstPerson =
				bUseOverShoulder &&
				bIsZooming &&
				ConfigManager::B("Misc.OTSADSOverride") &&
				ConfigManager::B("Misc.OTSADSFirstPerson");
			if (bOTSAdsFirstPerson)
			{
				bShouldBeInThirdPerson = false;
			}
			else if (bUseThirdPerson && !bUseOverShoulder && bIsZooming && ConfigManager::B("Misc.ThirdPersonADSFirstPerson"))
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
	}
	else
	{
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
	if (ConfigManager::B("Player.Freecam"))
	{
		ConfigManager::B("Player.ThirdPerson") = false;
		ConfigManager::B("Player.OverShoulder") = false;
	}

	if (ConfigManager::B("Player.OverShoulder") && !ConfigManager::B("Player.Freecam"))
	{
		ConfigManager::B("Player.ThirdPerson") = true;
	}

	RegisterNativeCameraHookSignature();

	SignatureRegistry::EnsureHook(
		kNativeCameraUpdateSignature,
		reinterpret_cast<void*>(&hkNativeCameraUpdate),
		reinterpret_cast<void**>(&oNativeCameraUpdate),
		kNativeCameraUpdateHookLen,
		"CamNative",
		true);

	if (g_BlockNativeCameraUntilGameplayReady)
	{
		if (!IsGameplayReadyForNativeCamera())
		{
			ResetNativeCameraState();
			ResetNativeFreecamState();
			return;
		}

		Logger::LogThrottled(Logger::Level::Debug, "Camera", 2000, "Gameplay became ready again, allowing native camera overrides");
		g_BlockNativeCameraUntilGameplayReady = false;
	}

	if (Utils::bIsLoading || !Utils::bIsInGame)
	{
		ResetNativeCameraState();
		ResetNativeFreecamState();
		return;
	}

	Cheats::ChangeFOV();
	UpdateCameraModes();

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
	ResetNativeCameraState();
	ResetNativeFreecamState();
}

bool Cheats::ShouldTraceNativeCamera()
{
#if BL4_DEBUG_BUILD
	return ConfigManager::B("Misc.Debug") &&
		!g_BlockNativeCameraUntilGameplayReady &&
		!Utils::bIsLoading &&
		Utils::bIsInGame &&
		IsNativeOTSGameplayReady();
#else
	return false;
#endif
}

void Cheats::ApplyNativeCameraPostUpdate(uintptr_t cameraContext, float deltaSeconds)
{
	__try
	{
		ApplyNativeCameraPostUpdateImpl(cameraContext, deltaSeconds);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		g_BlockNativeCameraUntilGameplayReady = true;
		ResetNativeCameraState();
		ResetNativeFreecamState();
	}
}

bool Cheats::HandleCameraEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	(void)Params;
	if (ShouldForceTravelCameraReset(Object, Function))
	{
		if (!g_BlockNativeCameraUntilGameplayReady)
		{
			const std::string functionName = Function ? Function->GetName() : "Unknown";
			const std::string className = (Object && Object->Class) ? Object->Class->GetName() : "Unknown";
			Logger::LogThrottled(Logger::Level::Debug, "Camera", 250, "ProcessEvent detected travel/menu transition (%s::%s), blocking native camera overrides", className.c_str(), functionName.c_str());
		}

		g_BlockNativeCameraUntilGameplayReady = true;
		ResetNativeCameraState();
		ResetNativeFreecamState();
	}

	if (Object == GVars.PlayerController ||
		(GVars.PlayerController && Object == GVars.PlayerController->PlayerCameraManager))
	{
		const std::string functionName = Function ? Function->GetName() : "";
		if (functionName == "ReceiveEndPlay" ||
			functionName == "EndPlay" ||
			functionName == "ReceiveDestroyed" ||
			functionName == "Destroyed" ||
			functionName == "BeginDestroy")
		{
			g_BlockNativeCameraUntilGameplayReady = true;
			ResetNativeCameraState();
			ResetNativeFreecamState();
		}
	}

	return false;
}
