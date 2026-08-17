#pragma once

#include "CoreMinimal.h"
#include "BattleState.h"

struct FLevelLoadResult
{
	bool bSuccess = false;
	SimRTS::Level Level;
	FString Error;
};

/**
 * Parse split JSON docs (Unreal Json module) into one RTSEngine Level.
 *
 * Level file:  world width/height + obstruction map
 * Rules file:  ticks_per_second + unit_defs
 * Spawns file: spawns array
 */
namespace LevelLoader
{
	FLevelLoadResult ParseLevelJsonString(const FString& JsonText);
	FLevelLoadResult ParseRulesJsonString(const FString& JsonText);
	FLevelLoadResult ParseSpawnsJsonString(const FString& JsonText);

	FLevelLoadResult MergeParts(
		const FLevelLoadResult& LevelPart,
		const FLevelLoadResult& RulesPart,
		const FLevelLoadResult& SpawnsPart);

	FLevelLoadResult LoadFromFiles(
		const FString& LevelFilePath,
		const FString& RulesFilePath,
		const FString& SpawnsFilePath);
}
