#pragma once

namespace Features::Debug
{
	bool OnEvent(const Core::SchedulerGameEvent& Event);

	void DumpObjects();
	void Update();
}
