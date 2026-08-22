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

	/** Load default level / rules / spawns, spawn unit actors, start the sim clock. */
	bool LoadDefault(ASimRTSGameMode& GameMode);

	void Stop(ASimRTSGameMode& GameMode);

	bool IsLoaded() const { return bLoaded; }

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
};
