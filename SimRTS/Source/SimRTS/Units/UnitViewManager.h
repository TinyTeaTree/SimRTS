#pragma once

#include "CoreMinimal.h"
#include "Types.h"
#include "UObject/Object.h"
#include "UnitViewManager.generated.h"

class ASimRTSGameMode;
class ASimRTSUnitActor;

/**
 * Owns UnitType → actor class mapping and live UnitId → actor instances.
 * Display-only; does not touch RTSEngine rules.
 */
UCLASS()
class SIMRTS_API UUnitViewManager : public UObject
{
	GENERATED_BODY()

public:
	UUnitViewManager();

	void SetActorClassForType(SimRTS::UnitType Type, TSubclassOf<ASimRTSUnitActor> ActorClass);
	TSubclassOf<ASimRTSUnitActor> GetActorClassForType(SimRTS::UnitType Type) const;

	void RebuildActors(ASimRTSGameMode* GameMode);
	void SyncActors(const ASimRTSGameMode* GameMode) const;

	ASimRTSUnitActor* FindActor(int32 UnitId) const;

private:
	UPROPERTY()
	TMap<uint8, TSubclassOf<ASimRTSUnitActor>> ActorClassesByType;

	UPROPERTY()
	TMap<int32, TObjectPtr<ASimRTSUnitActor>> UnitActors;
};
