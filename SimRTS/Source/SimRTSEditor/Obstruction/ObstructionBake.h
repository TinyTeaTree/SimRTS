#pragma once

#include "CoreMinimal.h"

struct FObstructionBakeParams
{
	FString JsonPath;
	float GridScale = 10.f;
};

struct FObstructionBakeResult
{
	bool bSuccess = false;
	FString Error;
	int32 Width = 0;
	int32 Height = 0;
	int32 BlockedCells = 0;
	int32 VolumeCount = 0;
};

/** Bake placed ASimRTSObstructionVolume actors into the level JSON obstruction string. */
namespace ObstructionBake
{
	FObstructionBakeResult BakeToJsonFile(const FObstructionBakeParams& Params);
}
