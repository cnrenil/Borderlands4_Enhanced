#pragma once

namespace Features::World
{
	void KillEnemies();
	void SpawnItems();
	void TeleportLoot();
	void ClearGroundItems();
	void SetGameSpeed(float speed);
	void SetPlayersOnly(bool enabled);
	bool OnEvent(const Core::SchedulerGameEvent& Event);
}
