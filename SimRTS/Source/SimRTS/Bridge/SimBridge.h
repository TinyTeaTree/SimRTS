#pragma once

#include "CoreMinimal.h"
#include "TickEngine.h"

// Thin Unreal-side holder for the RTSEngine module (TickEngine).
// Loads/parses level JSON in SimRTS, then seeds TickEngine with a Level.
// Selection stays in Unreal; orders are submitted into the engine.
class SIMRTS_API FSimBridge
{
public:
	/** Load DefaultLevel + GameRules + DefaultSpawns JSON. Returns false if missing or invalid. */
	bool ResetToDefaultLevel();

	void SubmitMoveOrder(
		const TArray<int32>& UnitIds,
		int32 TargetX,
		int32 TargetY,
		bool bIsNext = false,
		int32 PlayerId = 0);
	void StepForward();

	int32 GetTick() const;
	const SimRTS::BattleState& GetState() const;
	const SimRTS::StaticBattleData& GetStaticData() const;

	SimRTS::TickEngine& GetEngine() { return Engine; }
	const SimRTS::TickEngine& GetEngine() const { return Engine; }

private:
	SimRTS::TickEngine Engine;
};
