#include "pch.h"
#include <Windows.h>

#include "Cheats.h"
#include "Utils/Utils.h"

using namespace SDK;

void Cheats::ToggleGodMode() {
	if (!GVars.PlayerController) return;
	if (!GVars.Character) return;
	
    // UE standard God Mode typically modifies bCanBeDamaged
    // But this can be expanded with BL4 SDK specific God Mode later
    if (GVars.Character) {
        GVars.Character->bCanBeDamaged = !CVars.GodMode;
    }
}
