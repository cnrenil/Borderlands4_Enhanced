#include "pch.h"

void Cheats::SilentAimHoming()
{
    bHasSilentAimTarget = false;
	if (!ConfigManager::B("SilentAim.Enabled") || !GVars.Character || !GVars.PlayerController || !GVars.World) return;

	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("SilentAim.RequiresLOS"),
		ConfigManager::S("SilentAim.Bone"),
		ConfigManager::B("SilentAim.TargetAll")
	);

	if (!Target || !Target->IsA(ACharacter::StaticClass())) return;

	FVector TargetPos;
	ACharacter* TargetChar = reinterpret_cast<ACharacter*>(Target);

	if (TargetChar->Mesh) {
		std::wstring BoneWStr = UtfN::StringToWString(ConfigManager::S("SilentAim.Bone"));
		FName BoneName = UKismetStringLibrary::Conv_StringToName(BoneWStr.c_str());
		if (TargetChar->Mesh->GetBoneIndex(BoneName) != -1)
		{
			TargetPos = TargetChar->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
		}
		else
		{
			TargetPos = Utils::GetHighestBone(TargetChar);
		}
	} else {
		TargetPos = Target->K2_GetActorLocation();
	}

    // Update Cache for rendering
    bHasSilentAimTarget = true;
    SilentAimTargetPos = TargetPos;
}
