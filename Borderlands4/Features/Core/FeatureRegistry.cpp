#include "pch.h"

namespace Features
{
	void RegisterAllFeatures()
	{
		RegisterAimbotFeature();
		RegisterPlayerFeature();
		RegisterESPFeature();
		RegisterPersistenceFeature();
		RegisterMovementFeature();
		RegisterWorldFeature();
		RegisterCameraFeature();
		RegisterDebugFeature();
		RegisterTriggerFeature();
		RegisterWeaponFeature();
		RegisterReticleFeature();
		RegisterEnabledOptionsFeature();
		GUI::RegisterMenu();
	}
}
