#pragma once

namespace Features::Player
{
	void SetGodMode(bool enabled);
	void SetDemigod(bool enabled);
	void SetInfiniteAmmo(bool enabled);
	void SetInfiniteGrenades(bool enabled);
	void SetNoTarget(bool enabled);
	void AddCurrency(const std::string& type, int32_t amount);
	void GiveLevels();
	void SetExperienceLevel(int32_t level);

	void Update();
}
