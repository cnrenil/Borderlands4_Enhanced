#pragma once

namespace Features::Weapon
{
	bool HandleWeaponEvent(const Core::SchedulerGameEvent& event);
	void Update();
	void SetInfiniteAmmo(bool enabled);
	void SetNoRecoil(bool enabled);
	void SetNoSpread(bool enabled);
	void SetRapidFire(bool enabled);
	// etc.
}
