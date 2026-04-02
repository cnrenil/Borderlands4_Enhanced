#pragma once

namespace Features
{
	void RegisterAimbotFeature();
	
	namespace Aimbot
	{
		void RunTick();
		void RunHotkey();
		void HandleConstructedObject(const SDK::UObject* object);
	}
}
