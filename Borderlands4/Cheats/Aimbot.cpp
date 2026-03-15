#include "pch.h"

static SDK::AActor* CurrentAimbotTarget = nullptr;

void Cheats::Aimbot()
{
	bHasAimbotTarget = false;
	CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame)
	{
		SilentAimHooks::UpdateTarget(nullptr, FVector{});
		return;
	}
	if (!GVars.POV || !GVars.PlayerController || !Utils::GetSelfActor())
	{
		SilentAimHooks::UpdateTarget(nullptr, FVector{});
		return;
	}

	if (ConfigManager::B("Aimbot.Silent"))
	{
		SilentAimHooks::Tick();
	}

	CurrentAimbotTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (CurrentAimbotTarget && CurrentAimbotTarget->IsA(ACharacter::StaticClass()))
	{
		ACharacter* targetChar = reinterpret_cast<ACharacter*>(CurrentAimbotTarget);

		static std::string cachedBoneString;
		static FName cachedBoneName;
		if (cachedBoneString != ConfigManager::S("Aimbot.Bone"))
		{
			std::wstring wideString = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
			cachedBoneName = UKismetStringLibrary::Conv_StringToName(wideString.c_str());
			cachedBoneString = ConfigManager::S("Aimbot.Bone");
		}

		FVector targetPos;
		if (targetChar->Mesh && targetChar->Mesh->GetBoneIndex(cachedBoneName) != -1)
			targetPos = targetChar->Mesh->GetBoneTransform(cachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
		else
			targetPos = Utils::GetHighestBone(targetChar);

		bHasAimbotTarget = true;
		AimbotTargetPos = targetPos;
		SilentAimHooks::UpdateTarget(CurrentAimbotTarget, AimbotTargetPos);
		return;
	}

	SilentAimHooks::UpdateTarget(nullptr, FVector{});
}

void Cheats::AimbotHotkey()
{
	SilentAimHooks::OnAimbotHotkey();

	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.POV)
	{
		SilentAimHooks::ResetArm();
		return;
	}

	if (ConfigManager::B("Aimbot.Silent"))
	{
		return;
	}

	if (!CurrentAimbotTarget)
	{
		return;
	}

	SilentAimHooks::ResetArm();

	const FVector cameraPos = GVars.POV->Location;
	const FVector targetPos = AimbotTargetPos;
	FRotator desiredRot = Utils::GetRotationToTarget(cameraPos, targetPos);
	FRotator currentRot = GVars.PlayerController->ControlRotation;

	if (ConfigManager::B("Aimbot.Smooth"))
	{
		float smoothFactor = (ConfigManager::F("Aimbot.SmoothingVector") <= 1.0f)
			? 1.0f
			: ConfigManager::F("Aimbot.SmoothingVector");
		FRotator delta = desiredRot - currentRot;
		delta.Normalize();

		FRotator smoothedRot = currentRot + (delta / smoothFactor);
		smoothedRot.Normalize();
		GVars.PlayerController->ClientSetRotation(smoothedRot, true);
	}
	else
	{
		GVars.PlayerController->ClientSetRotation(desiredRot, true);
	}
}

bool Cheats::HandleAimbotEvents(const SDK::UObject* object, SDK::UFunction* function, void* params)
{
	(void)object;
	(void)function;
	(void)params;
	return false;
}

void Cheats::HandleConstructedObject(const SDK::UObject* object)
{
	(void)object;
}
