#include "pch.h"

static AActor* CurrentAimbotTarget = nullptr;

void Cheats::Aimbot()
{
    bHasAimbotTarget = false;
    CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame) return;
	if (!GVars.POV || !GVars.PlayerController || !GVars.Character) return;

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
	// 真正自瞄逻辑：负责旋转相机视角
	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.POV || !CurrentAimbotTarget) return;

    ACharacter* TargetChar = reinterpret_cast<ACharacter*>(CurrentAimbotTarget);
    FVector CameraPos = GVars.POV->Location;
    FVector TargetPos = AimbotTargetPos;

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
