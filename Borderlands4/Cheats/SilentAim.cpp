#include "pch.h"
#include "Engine.h"


void Cheats::SilentAimHoming()
{
	if (!WeaponSettings.HomingProjectiles || !GVars.Character || !GVars.PlayerController || !GVars.World) return;

	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		AimbotSettings.MaxFOV,
		SilentAimSettings.RequiresLOS,
		TextVars.AimbotBone,
		AimbotSettings.TargetAll
	);

	if (!Target || !Target->IsA(ACharacter::StaticClass())) return;

	FVector TargetPos;
	ACharacter* TargetChar = reinterpret_cast<ACharacter*>(Target);

	if (TargetChar->Mesh) {
		std::wstring BoneWStr = UtfN::StringToWString(TextVars.AimbotBone);
		FName BoneName = UKismetStringLibrary::Conv_StringToName(BoneWStr.c_str());
		if (TargetChar->Mesh->GetBoneIndex(BoneName) != -1)
		{
			TargetPos = TargetChar->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
		}
		else
		{
			// Fallback to the highest bone if targeted bone is not found
			TargetPos = Utils::GetHighestBone(TargetChar);
		}
	} else {
		TargetPos = Target->K2_GetActorLocation();
	}

	// Draw Tracer Line for Silent Aim if enabled
	if (AimbotSettings.DrawArrow)
	{
		Utils::DrawSnapLine(TargetPos, AimbotSettings.ArrowThickness);
	}

	TArray<AActor*> Projectiles;
	UGameplayStatics::GetAllActorsOfClass(Utils::GetWorldSafe(), AOakProjectile::StaticClass(), &Projectiles);
	
	for (int i = 0; i < Projectiles.Num(); i++) {
		AOakProjectile* Proj = static_cast<AOakProjectile*>(Projectiles[i]);
		if (Utils::IsValidActor(Proj) && Proj->GetInstigator() == GVars.Character) {
			float dist = Proj->K2_GetActorLocation().GetDistanceToInMeters(TargetPos);
			// Only home in if within sensible range and not already too close
			if (dist > 0.5f && dist < WeaponSettings.HomingRange) {
				Proj->K2_SetActorLocation(TargetPos, false, nullptr, false);
			}
		}
	}
}

