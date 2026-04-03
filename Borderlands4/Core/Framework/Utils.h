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

struct TargetSelectionResult
{
	AActor* Target = nullptr;
	FVector AimPoint{};
	FVector2D ScreenLocation{};
	float DistanceMeters = FLT_MAX;
	float ScreenSpaceFOV = FLT_MAX;
	bool bHasLOS = false;

	bool IsValid() const { return Target != nullptr; }
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
	static AActor* GetSelfActor(); // character if available, otherwise controlled pawn/vehicle
	static class ALightProjectileManager* GetLightProjManager();
	static unsigned ConvertImVec4toU32(ImVec4 Color);
	static void PrintActors(const char* Exclude);
	static FRotator VectorToRotation(const FVector& Vec);
	static TargetSelectionResult AcquireTarget(APlayerController* ViewPoint, float MaxFOV, float MinDistance, float MaxDistance, bool RequiresLOS, std::string TargetBone, bool TargetAll, int TargetMode);
	static bool ForEachLevelActor(ULevel* Level, const std::function<bool(AActor*)>& Visitor, int32_t* OutActorCount = nullptr);
	static bool ForEachLevelActorChunk(ULevel* Level, int32_t StartIndex, int32_t MaxActors, const std::function<bool(AActor*)>& Visitor, int32_t* OutNextIndex = nullptr, int32_t* OutActorCount = nullptr, bool* OutCompleted = nullptr);
	static float GetDistanceMeters(const FVector& A, const FVector& B);
	static float GetDistanceMeters(const AActor* Source, const AActor* Target);
	static void DrawFOV(float MaxFOV, float Thickness);
	static void DrawSnapLine(FVector TargetPos, float Thickness);
	static void SetCurrentCanvas(class UCanvas* Canvas);
	static class UCanvas* GetCurrentCanvas();
	static bool ProjectWorldLocationToScreen(const FVector& WorldLocation, FVector2D& OutScreenLocation, bool bPlayerViewportRelative = true);
	static bool ProjectWorldLocationToScreen(class APlayerController* PlayerController, const FVector& WorldLocation, FVector2D& OutScreenLocation, bool bPlayerViewportRelative = true);
	static FLinearColor ImVec4ToLinearColor(const ImVec4& Color);
	static FLinearColor U32ToLinearColor(ImU32 Color);
	static FVector FRotatorToVector(const FRotator& Rot);
	static PlayerCheatData& GetPlayerCheats(ACharacter* Player);
	static bool IsValidActor(AActor* Actor);
	static float GetFOVFromScreenCoords(const ImVec2& ScreenLocation);
	static ImVec2 FVector2DToImVec2(FVector2D Vector);
	static FRotator GetRotationToTarget(const FVector& Start, const FVector& Target);
	static FVector2D ImVec2ToFVector2D(ImVec2 Vector);
	static ACharacter* GetNearestCharacter(ETeam Team);
	static void Error(std::string msg);
	static ETeamAttitude GetAttitude(AActor* Target);
	static float GetHealthPercent(AActor* Actor);
	static bool GetReliableMeshBounds(ACharacter* TargetChar, FVector& OutOrigin, FVector& OutExtent);
	static FVector GetHighestBone(ACharacter* TargetChar);
	static FVector GetBestAimPoint(ACharacter* TargetChar, const std::string& PreferredBone);
	static void SendMouseLeftDown();
	static void SendMouseLeftUp();
};

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
	if (L <= 0.0001f) return FVector{ 0, 0, 0 };
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
