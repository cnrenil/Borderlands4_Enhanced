#pragma once

namespace Features::Debug
{
	void HandleEvents(
		const SDK::UObject* Object,
		SDK::UFunction* Function,
		void* Params,
		void(*OriginalProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*),
		bool bCallOriginal);

	void DumpObjects();
	void Update();
}
