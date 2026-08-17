#pragma once

#include "CoreMinimal.h"

struct FSpawnBakeParams
{
	/** Level JSON — used only to read world width/height for world→grid. */
	FString LevelJsonPath;
	/** Spawns JSON written by the bake (`spawns` array). */
	FString SpawnsJsonPath;
	float GridScale = 10.f;
};

struct FSpawnBakeResult
{
	bool bSuccess = false;
	FString Error;
	int32 Width = 0;
	int32 Height = 0;
	int32 SpawnCount = 0;
};

/** Bake placed ASimRTSSpawnMarker actors into the spawns JSON file. */
namespace SpawnBake
{
	FSpawnBakeResult BakeToJsonFile(const FSpawnBakeParams& Params);
}
