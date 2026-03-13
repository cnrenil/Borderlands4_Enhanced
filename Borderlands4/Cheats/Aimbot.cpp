#include "pch.h"

static AActor* CurrentAimbotTarget = nullptr;
namespace
{
	static SDK::FVector g_SilentRedirectTargetPos{};
	static double g_LastSilentArmTime = 0.0;

	bool IsLocalLightProjectileSpawn(const SDK::FLightProjectileSpawnData& SpawnData)
	{
		SDK::AActor* localCharacter = GVars.Character;
		SDK::AActor* controlledPawn = (GVars.PlayerController ? static_cast<SDK::AActor*>(GVars.PlayerController->Pawn) : nullptr);
		SDK::AActor* instigator = SpawnData.instigator.Get();
		SDK::AActor* source = SpawnData.Source.Get();
		SDK::AActor* damageCauser = SpawnData.DamageCauser.Get();

		if (localCharacter && (instigator == localCharacter || source == localCharacter || damageCauser == localCharacter))
			return true;
		if (controlledPawn && (instigator == controlledPawn || source == controlledPawn || damageCauser == controlledPawn))
			return true;
		return false;
	}

	bool RedirectLightProjectileSpawnDirection(SDK::FLightProjectileSpawnData& SpawnData, const SDK::FVector& TargetPos)
	{
		const SDK::FVector delta = TargetPos - SpawnData.Location;
		const float lenSq = (float)(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
		if (lenSq < 0.0001f) return false;

		const float len = sqrtf(lenSq);
		SDK::FVector newDirection{ delta.X / len, delta.Y / len, delta.Z / len };
		SpawnData.Direction = newDirection;
		SpawnData.EndLocation = TargetPos;
		return true;
	}
}

void Cheats::Aimbot()
{
	bHasAimbotTarget = false;
	CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame) return;
	if (!GVars.POV || !GVars.PlayerController || !Utils::GetSelfActor()) return;

	// Ordinary logic: Target acquisition and visual state
	CurrentAimbotTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (CurrentAimbotTarget && CurrentAimbotTarget->IsA(ACharacter::StaticClass())) 
	{
		ACharacter* TargetChar = reinterpret_cast<ACharacter*>(CurrentAimbotTarget);
		
		static std::string CachedBoneString = "";
		static FName CachedBoneName;
		if (CachedBoneString != ConfigManager::S("Aimbot.Bone")) {
			std::wstring WideString = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
			CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
			CachedBoneString = ConfigManager::S("Aimbot.Bone");
		}

		FVector TargetPos;
		if (TargetChar->Mesh && TargetChar->Mesh->GetBoneIndex(CachedBoneName) != -1)
			TargetPos = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
		else
			TargetPos = Utils::GetHighestBone(TargetChar);

		// Cache for snaplines
		bHasAimbotTarget = true;
		AimbotTargetPos = TargetPos;
	}
}

void Cheats::AimbotHotkey()
{
	// 真正自瞄逻辑：负责旋转相机视角 / 静默重定向武装
	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.POV || !CurrentAimbotTarget)
	{
		g_LastSilentArmTime = 0.0;
		return;
	}

    FVector CameraPos = GVars.POV->Location;
    FVector TargetPos = AimbotTargetPos;

	if (ConfigManager::B("Aimbot.Silent"))
	{
		g_SilentRedirectTargetPos = TargetPos;
		g_LastSilentArmTime = ImGui::GetTime();
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			250,
			"Silent redirect armed. target=(%.1f, %.1f, %.1f)",
			TargetPos.X, TargetPos.Y, TargetPos.Z);
		return;
	}
	g_LastSilentArmTime = 0.0;

    // Actual execution of rotation
	FRotator DesiredRot = Utils::GetRotationToTarget(CameraPos, TargetPos);
	FRotator CurrentRot = GVars.PlayerController->ControlRotation;

	if (ConfigManager::B("Aimbot.Smooth"))
	{
		float SmoothFactor = (ConfigManager::F("Aimbot.SmoothingVector") <= 1.0f) ? 1.0f : ConfigManager::F("Aimbot.SmoothingVector");
		FRotator Delta = DesiredRot - CurrentRot;
		Delta.Normalize();

		FRotator SmoothedRot = CurrentRot + (Delta / SmoothFactor);
		SmoothedRot.Normalize();
		GVars.PlayerController->ClientSetRotation(SmoothedRot, true);
	}
	else
	{
		GVars.PlayerController->ClientSetRotation(DesiredRot, true);
	}
}

bool Cheats::HandleAimbotEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	if (!ConfigManager::B("Aimbot.Enabled") || !ConfigManager::B("Aimbot.Silent")) return false;
	if (!Object || !Function || !Params || !Object->Class) return false;

	const std::string functionName = Function->GetName();
	if (Object->Class->GetName().find("LightProjectileStatics") == std::string::npos ||
		functionName.find("SpawnLightProjectile") == std::string::npos)
	{
		return false;
	}

	const double now = ImGui::GetTime();
	const bool bArmed = (now - g_LastSilentArmTime) <= 0.15;
	if (!bArmed)
	{
		Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Spawn seen but silent redirect not armed (hotkey not held).");
		return false;
	}

	struct SpawnLightProjectileParamsPrefix
	{
		SDK::FLightProjectileSpawnData SpawnData;
	};

	auto* spawnParams = reinterpret_cast<SpawnLightProjectileParamsPrefix*>(Params);
	if (!spawnParams) return false;

	if (!IsLocalLightProjectileSpawn(spawnParams->SpawnData))
	{
		Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Spawn seen but not local projectile; skip.");
		return false;
	}

	Logger::LogThrottled(
		Logger::Level::Debug,
		"SilentAim",
		300,
		"Intercepted %s. spawn=(%.1f, %.1f, %.1f) target=(%.1f, %.1f, %.1f)",
		functionName.c_str(),
		spawnParams->SpawnData.Location.X,
		spawnParams->SpawnData.Location.Y,
		spawnParams->SpawnData.Location.Z,
		g_SilentRedirectTargetPos.X,
		g_SilentRedirectTargetPos.Y,
		g_SilentRedirectTargetPos.Z);

	if (RedirectLightProjectileSpawnDirection(spawnParams->SpawnData, g_SilentRedirectTargetPos))
	{
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			300,
			"Projectile redirected. newDir=(%.3f, %.3f, %.3f)",
			spawnParams->SpawnData.Direction.X,
			spawnParams->SpawnData.Direction.Y,
			spawnParams->SpawnData.Direction.Z);
	}
	else
	{
		Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Redirect skipped: spawn location too close to target.");
	}

	return false;
}
