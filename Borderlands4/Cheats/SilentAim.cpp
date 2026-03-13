#include "pch.h"

void Cheats::SilentAimHoming()
{
    bHasSilentAimTarget = false;
	if (!ConfigManager::B("Weapon.HomingProjectiles") || !GVars.Character || !GVars.PlayerController || !GVars.World) return;

	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("SilentAim.RequiresLOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (!Target || !Target->IsA(ACharacter::StaticClass())) return;

	FVector TargetPos;
	ACharacter* TargetChar = reinterpret_cast<ACharacter*>(Target);

	if (TargetChar->Mesh) {
		std::wstring BoneWStr = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
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

	TArray<AActor*> Projectiles;
	UGameplayStatics::GetAllActorsOfClass(Utils::GetWorldSafe(), AOakProjectile::StaticClass(), &Projectiles);
	
	for (int i = 0; i < Projectiles.Num(); i++) {
		AOakProjectile* Proj = static_cast<AOakProjectile*>(Projectiles[i]);
		if (Utils::IsValidActor(Proj) && Proj->GetInstigator() == GVars.Character) {
			float dist = Proj->K2_GetActorLocation().GetDistanceToInMeters(TargetPos);
			if (dist > 0.5f && dist < ConfigManager::F("Weapon.HomingRange")) {
				Proj->K2_SetActorLocation(TargetPos, false, nullptr, false);
			}
		}
	}
}
