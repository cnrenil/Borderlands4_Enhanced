#pragma once
// Core utility types and constants
void cerrf(const char* Format, ...);
struct Colors
{
	static const ImU32 White = IM_COL32(255, 255, 255, 255);
	static const ImU32 Black = IM_COL32(0, 0, 0, 255);
	static const ImU32 Red = IM_COL32(255, 0, 0, 255);
	static const ImU32 Green = IM_COL32(0, 255, 0, 255);
	static const ImU32 Blue = IM_COL32(0, 0, 255, 255);
	static const ImU32 Yellow = IM_COL32(255, 255, 0, 255);
	static const ImU32 Cyan = IM_COL32(0, 255, 255, 255);
	static const ImU32 Magenta = IM_COL32(255, 0, 255, 255);
	static const ImU32 Gray = IM_COL32(128, 128, 128, 255);
	static const ImU32 Orange = IM_COL32(255, 165, 0, 255);
	static const ImU32 Purple = IM_COL32(128, 0, 128, 255);
	static const ImU32 Pink = IM_COL32(255, 192, 203, 255);
};

enum class ETeam
{
	TEAM_PLAYER = 0,
	TEAM_ENEMY,
	TEAM_ALLY,
	TEAM_MAX
};

// Per-player cheat settings
struct PlayerCheatData
{
	bool GodMode = false;
	bool InfAmmo = false;

	PlayerCheatData() = default;
};

inline std::unordered_map<ACharacter*, PlayerCheatData> PlayerCheatMap;



inline float GetDistance(AActor* Actor, FVector AActorLocation)
{
	if (!Actor || !Actor->RootComponent)
		return -1.0f;
	const auto RootComponent = Actor->RootComponent;
	auto deltaX = (float)(RootComponent->RelativeLocation.X - AActorLocation.X);
	auto deltaY = (float)(RootComponent->RelativeLocation.Y - AActorLocation.Y);
	auto deltaZ = (float)(RootComponent->RelativeLocation.Z - AActorLocation.Z);

	return (float)std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

struct Utils
{
	static UWorld* GetWorldSafe(); // can return nullptr
	static APlayerController* GetPlayerController(); // can return nullptr
	static class ALightProjectileManager* GetLightProjManager();
	static unsigned ConvertImVec4toU32(ImVec4 Color);
	static void PrintActors(const char* Exclude);
	static FRotator VectorToRotation(const FVector& Vec);
	static AActor* GetBestTarget(APlayerController* ViewPoint, float MaxFOV, bool RequiresLOS, std::string TargetBone, bool TargetAll);
	static void DrawFOV(float MaxFOV, float Thickness);
	static void DrawSnapLine(FVector TargetPos, float Thickness);
	static FVector FRotatorToVector(const FRotator& Rot);
	static PlayerCheatData& GetPlayerCheats(ACharacter* Player);
	static bool IsValidActor(AActor* Actor);
	static float GetFOVFromScreenCoords(const ImVec2& ScreenLocation);
	static ImVec2 FVector2DToImVec2(FVector2D Vector);
	static FRotator GetRotationToTarget(const FVector& Start, const FVector& Target);
	static FVector2D ImVec2ToFVector2D(ImVec2 Vector);
	static ACharacter* GetNearestCharacter(ETeam Team);
	static void Error(std::string msg);
	static bool IsInLoadingState();
	static ETeamAttitude GetAttitude(AActor* Target);
	static float GetHealthPercent(AActor* Actor);
	static FVector GetHighestBone(ACharacter* TargetChar);

	static inline bool bIsLoading = false;
};

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
	SDK::ACameraActor* CameraActor = nullptr;

	Variables() {
		Reset();
	}

	void Reset() {
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
		this->CameraActor = nullptr;
	}

	void UpdateUnitCache() {
		if (!this->Level) return;
		this->UnitCache.clear();

		int ActorCount = this->Level->Actors.Num();
		if (ActorCount <= 0 || ActorCount > 100000) return;

		for (int i = 0; i < ActorCount; i++) {
			AActor* Actor = this->Level->Actors[i];
			if (!Actor || !Utils::IsValidActor(Actor)) continue;

			if (Actor->IsA(ACharacter::StaticClass())) {
				this->UnitCache.push_back(reinterpret_cast<ACharacter*>(Actor));
			}
		}
	}

	void AutoSetVariables() {
		UWorld* currentWorld = Utils::GetWorldSafe();

		// If world is null or changed, reset everything dependent on it
		if (!currentWorld || this->World != currentWorld) {
			if (this->World != currentWorld) {
				// World actually changed - clear stale data
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

		if (!this->World || !this->World->VTable)
		{
			Utils::bIsLoading = true;
			return;
		}

		this->Level = this->World->PersistentLevel;
		if (!this->Level) return;

		this->GameState = this->World->GameState;

		// Update loading state
		Utils::bIsLoading = Utils::IsInLoadingState();
		if (Utils::bIsLoading) return;

		// Periodically update the character cache (every 60 frames ~ 1s at 60fps)
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

		if (!this->PlayerController || !this->PlayerController->VTable) return;

		// 4. Update PC dependents (Pawn/Character)
		if (this->PlayerController->Pawn && this->PlayerController->Pawn->VTable) {
			this->Pawn = this->PlayerController->Pawn;
		}
		else {
			this->Pawn = nullptr;
		}

		if (this->PlayerController->Character && this->PlayerController->Character->VTable) {
			this->Character = this->PlayerController->Character;
		}
		else {
			this->Character = nullptr;
		}

		// 5. Update Camera POV
		if (this->PlayerController->PlayerCameraManager && this->PlayerController->PlayerCameraManager->VTable) {
			this->POV = &this->PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
		}
		else {
			this->POV = nullptr;
		}

		// Update Screen size
		if (ImGui::GetCurrentContext())
			ScreenSize = ImGui::GetIO().DisplaySize;
	}
} inline GVars;

static inline float Dot3(const FVector& A, const FVector& B)
{
	return (float)A.X * (float)B.X + (float)A.Y * (float)B.Y + (float)A.Z * (float)B.Z;
}

static inline float Length3(const FVector& V)
{
	return sqrtf((float)V.X * (float)V.X + (float)V.Y * (float)V.Y + (float)V.Z * (float)V.Z);
}

static inline FVector Normalize(const FVector& V)
{
	float L = Length3(V);
	if (L <= 0.0001f) return FVector{ 0,0,0 };
	return FVector{ V.X / L, V.Y / L, V.Z / L };
}

static inline float ClampFloat(float v, float a, float b)
{
	return v < a ? a : (v > b ? b : v);
}

static inline float AngleDegFromDot(float Dot)
{
	Dot = ClampFloat(Dot, -1.0f, 1.0f);
	return acosf(Dot) * (180.0f / 3.1415926535f);
}

static inline void ClampRotator(FRotator& R)
{
	R.Normalize();

	if (R.Pitch > 89.f)  R.Pitch = 89.f;
	if (R.Pitch < -89.f) R.Pitch = -89.f;
	R.Roll = 0.f;
}

static inline FVector ForwardFromRot(const FRotator& Rot)
{
	float PitchRad = (float)Rot.Pitch * (3.1415926535f / 180.0f);
	float YawRad = (float)Rot.Yaw * (3.1415926535f / 180.0f);

	float CP = cosf(PitchRad);
	return FVector{
		cosf(YawRad) * CP,
		sinf(YawRad) * CP,
		sinf(PitchRad)
	};
}
