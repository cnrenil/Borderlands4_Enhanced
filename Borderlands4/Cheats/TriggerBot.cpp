#include "pch.h"

static AActor* CurrentTriggerTarget = nullptr;

void Cheats::TriggerBot()
{
    CurrentTriggerTarget = nullptr;
	if (!ConfigManager::B("Trigger.Enabled") || !Utils::bIsInGame || !GVars.PlayerController || !GVars.Character) return;
	if (ImGui::GetIO().WantCaptureMouse) return;

	// Ordinary logic: Detection only
	CurrentTriggerTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		5.0f, // Narrow FOV for triggerbot
		true, // Must have Line Of Sight
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Trigger.TargetAll")
	);
}

void Cheats::TriggerHotkey()
{
	if (!Utils::bIsInGame || !GVars.PlayerController || !CurrentTriggerTarget) return;

	static bool bIsFiringUnderControl = false;
    
    if (CurrentTriggerTarget)
    {
        if (!bIsFiringUnderControl)
        {
            INPUT Input = { 0 };
            Input.type = INPUT_MOUSE;
            Input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &Input, sizeof(INPUT));
            bIsFiringUnderControl = true;
        }
    }
    else
    {
        if (bIsFiringUnderControl)
        {
            INPUT Input = { 0 };
            Input.type = INPUT_MOUSE;
            Input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &Input, sizeof(INPUT));
            bIsFiringUnderControl = false;
        }
    }
}
