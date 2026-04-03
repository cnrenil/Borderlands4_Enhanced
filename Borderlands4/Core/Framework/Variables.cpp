#include "pch.h"

Variables GVars{};
std::recursive_mutex gGVarsMutex;

Variables::Variables()
{
	Reset();
}

void Variables::Reset()
{
	this->World = nullptr;
	this->PlayerController = nullptr;
	this->GameState = nullptr;
	this->Pawn = nullptr;
	this->Character = nullptr;
	this->Level = nullptr;
	this->POV = nullptr;
	this->UnitCache.clear();
	this->CacheTimer = 0;
	this->ScreenSize = ImVec2(0, 0);
}

void Variables::UpdateUnitCache()
{
	if (!this->Level) return;
	this->UnitCache.clear();

	Utils::ForEachLevelActor(this->Level, [&](AActor* Actor)
	{
		if (!Actor || !Utils::IsValidActor(Actor)) return true;
		if (Actor->IsA(ACharacter::StaticClass()))
			this->UnitCache.push_back(reinterpret_cast<ACharacter*>(Actor));
		return true;
	});
}

void Variables::AutoSetVariables()
{
	UWorld* currentWorld = Utils::GetWorldSafe();
	if (!currentWorld || this->World != currentWorld) {
		if (this->World != currentWorld) {
			PlayerCheatMap.clear();
		}
		this->World = currentWorld;
		this->PlayerController = nullptr;
		this->GameState = nullptr;
		this->Pawn = nullptr;
		this->Character = nullptr;
		this->Level = nullptr;
		this->POV = nullptr;
		this->UnitCache.clear();
		this->CacheTimer = 0;
	}

	auto& state = Core::Scheduler::State();
	if (!this->World || !this->World->VTable)
	{
		state.IsLoading = true;
		state.IsInGame = false;
		return;
	}

	this->Level = this->World->PersistentLevel;
	if (!this->Level)
	{
		state.IsLoading = true;
		state.IsInGame = false;
		return;
	}

	this->GameState = this->World->GameState;

	bool bIsLoading = false;
	if (!this->World || !this->World->VTable) {
		bIsLoading = true;
	}
	else if (!this->Level || !this->Level->VTable) {
		bIsLoading = true;
	}
	else {
		int32_t actorCount = 0;
		__try
		{
			actorCount = this->Level->Actors.Num();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			actorCount = -1;
		}

		if (actorCount < 1 || actorCount > 200000)
			bIsLoading = true;
	}

	state.IsLoading = bIsLoading;
	if (state.IsLoading.load())
	{
		state.IsInGame = false;
		return;
	}

	if (CacheTimer <= 0) {
		UpdateUnitCache();
		CacheTimer = 30;
	}
	CacheTimer--;

	APlayerController* currentPC = Utils::GetPlayerController();
	if (!currentPC || this->PlayerController != currentPC) {
		this->PlayerController = currentPC;
		this->Pawn = nullptr;
		this->Character = nullptr;
		this->POV = nullptr;
	}

	if (!this->PlayerController || !this->PlayerController->VTable)
	{
		state.IsInGame = false;
		return;
	}

	this->Pawn = (this->PlayerController->Pawn && this->PlayerController->Pawn->VTable) ? this->PlayerController->Pawn : nullptr;
	this->Character = (this->PlayerController->Character && this->PlayerController->Character->VTable) ? this->PlayerController->Character : nullptr;
	this->POV = (this->PlayerController->PlayerCameraManager && this->PlayerController->PlayerCameraManager->VTable) ? &this->PlayerController->PlayerCameraManager->CameraCachePrivate.POV : nullptr;

	if (ImGui::GetCurrentContext())
		ScreenSize = ImGui::GetIO().DisplaySize;

	state.IsInGame = this->World && this->World->VTable && this->Level && this->GameState && this->PlayerController && this->PlayerController->VTable && this->PlayerController->PlayerCameraManager && this->PlayerController->PlayerCameraManager->VTable && Utils::GetSelfActor();
}
