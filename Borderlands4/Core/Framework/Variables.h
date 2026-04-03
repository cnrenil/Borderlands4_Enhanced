#pragma once

struct Variables
{
	APlayerController* PlayerController = nullptr;
	FMinimalViewInfo* POV = nullptr;
	APawn* Pawn = nullptr;
	ACharacter* Character = nullptr;
	UWorld* World = nullptr;
	AGameStateBase* GameState = nullptr;
	ULevel* Level = nullptr;
	std::vector<ACharacter*> UnitCache;
	int CacheTimer = 0;
	ImVec2 ScreenSize;

	Variables();
	void Reset();
	void UpdateUnitCache();
	void AutoSetVariables();
};

extern Variables GVars;
extern std::recursive_mutex gGVarsMutex;
