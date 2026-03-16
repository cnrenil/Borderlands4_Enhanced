#include "pch.h"


namespace
{
	AActor* g_CurrentAimbotTarget = nullptr;

	void ClearAimbotTarget()
	{
		Cheats::bHasAimbotTarget = false;
		g_CurrentAimbotTarget = nullptr;
		SilentAimHooks::UpdateTarget(nullptr, FVector{});
	}
}

void Cheats::Aimbot()
{
	bHasAimbotTarget = false;
	g_CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame)
	{
		ClearAimbotTarget();
		return;
	}
	if (!GVars.POV || !GVars.PlayerController || !Utils::GetSelfActor())
	{
		ClearAimbotTarget();
		return;
	}

	if (ConfigManager::B("Aimbot.Silent"))
	{
		SilentAimHooks::Tick();
	}

	const TargetSelectionResult targetResult = Utils::AcquireTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::F("Aimbot.MinDistance"),
		ConfigManager::F("Aimbot.MaxDistance"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll"),
		ConfigManager::I("Aimbot.TargetMode")
	);
	g_CurrentAimbotTarget = targetResult.Target;

	if (g_CurrentAimbotTarget && g_CurrentAimbotTarget->IsA(ACharacter::StaticClass()))
	{
		bHasAimbotTarget = true;
		AimbotTargetPos = targetResult.AimPoint;
		SilentAimHooks::UpdateTarget(g_CurrentAimbotTarget, AimbotTargetPos);
		return;
	}

	ClearAimbotTarget();
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

	if (!g_CurrentAimbotTarget)
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

bool Cheats::HandleAimbotEvents(const UObject* object, UFunction* function, void* params)
{
	(void)object;
	(void)function;
	(void)params;
	return false;
}

void Cheats::HandleConstructedObject(const UObject* object)
{
	(void)object;
}
