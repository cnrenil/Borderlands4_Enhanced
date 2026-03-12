#include "pch.h"

bool Init = false;

void Cheats::Aimbot()
{
	if (!CVars.Aimbot || Utils::bIsLoading) return;

	if (AimbotSettings.DrawFOV)
		Utils::DrawFOV(AimbotSettings.MaxFOV, AimbotSettings.FOVThickness);

	if (!GVars.POV || !GVars.PlayerController || !GVars.Level || !GVars.Character) return;

	// Aimbot key check (if required)
	bool AimbotKeyDown = ImGui::IsKeyDown(AimbotSettings.AimbotKey) || (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

	if (AimbotSettings.RequireKeyHeld && !AimbotKeyDown)
		return;

	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		AimbotSettings.MaxFOV,
		AimbotSettings.LOS,
		TextVars.AimbotBone,
		AimbotSettings.TargetAll
	);

	if (!Target || !Target->IsA(ACharacter::StaticClass())) return;

	ACharacter* TargetChar = reinterpret_cast<ACharacter*>(Target);

	static std::string CachedBoneString = "";
	static FName CachedBoneName;
	if (CachedBoneString != TextVars.AimbotBone) {
		std::wstring WideString = UtfN::StringToWString(TextVars.AimbotBone);
		CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
		CachedBoneString = TextVars.AimbotBone;
	}

	FVector CameraPos = GVars.POV->Location;
	if (!TargetChar->Mesh) return;

	FVector TargetPos;
	if (TargetChar->Mesh->GetBoneIndex(CachedBoneName) != -1)
	{
		TargetPos = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
	}
	else
	{
		// Fallback to the highest bone if the requested bone doesn't exist (e.g., drones, turrets)
		TargetPos = Utils::GetHighestBone(TargetChar);
	}

	double Dist = CameraPos.GetDistanceToInMeters(TargetPos);

	if (AimbotSettings.MinDistance > Dist || Dist > AimbotSettings.MaxDistance)
		return;

	if (AimbotSettings.DrawArrow)
		Utils::DrawSnapLine(TargetPos, AimbotSettings.ArrowThickness);

	// Simple target rotation calculation
	FRotator DesiredRot = Utils::GetRotationToTarget(CameraPos, TargetPos);
	FRotator CurrentRot = GVars.PlayerController->ControlRotation;

	if (AimbotSettings.Smooth)
	{
		float SmoothFactor = (AimbotSettings.SmoothingVector <= 1.0f) ? 1.0f : AimbotSettings.SmoothingVector;

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
