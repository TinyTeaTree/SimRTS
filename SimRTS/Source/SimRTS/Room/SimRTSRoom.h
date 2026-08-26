#pragma once

#include "CoreMinimal.h"
#include "SimBridge.h"
#include "UObject/Object.h"
#include "SimRTSRoom.generated.h"

class ASimRTSGameMode;
class UUnitViewManager;

/**
 * A play space: one TickEngine, a loaded level, and spawned unit views.
 * Loading a room is what used to happen immediately in GameMode BeginPlay.
 */
UCLASS()
class SIMRTS_API USimRTSRoom : public UObject
{
	GENERATED_BODY()

public:
	USimRTSRoom();

	/** Load default level / rules / spawns, spawn unit actors. Does not start the sim clock. */
	bool LoadDefault(ASimRTSGameMode& GameMode);

	/** Arm the Unreal sim timer. No-op if not loaded or already running. */
	void StartClock(ASimRTSGameMode& GameMode);

	void Stop(ASimRTSGameMode& GameMode);

	bool IsLoaded() const { return bLoaded; }
	bool IsClockStarted() const { return bClockStarted; }

	FSimBridge& GetBridge() { return Bridge; }
	const FSimBridge& GetBridge() const { return Bridge; }

	UUnitViewManager* GetUnitViewManager() const { return UnitViewManager; }

private:
	void OnSimTick();

	FSimBridge Bridge;

	UPROPERTY()
	TObjectPtr<UUnitViewManager> UnitViewManager;

	FTimerHandle SimTimerHandle;
	bool bLoaded = false;
	bool bClockStarted = false;
};
