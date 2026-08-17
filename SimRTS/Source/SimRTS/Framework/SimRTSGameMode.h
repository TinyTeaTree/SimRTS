#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimBridge.h"
#include "SimRTSGameMode.generated.h"

class ASimRTSUnitActor;
class UUnitViewManager;
class ASimRTSObstructionGridVisualizer;
class ASimRTSPathVisualizer;

UCLASS()
class SIMRTS_API ASimRTSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimRTSGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FSimBridge& GetBridge() { return Bridge; }
	const FSimBridge& GetBridge() const { return Bridge; }

	UUnitViewManager* GetUnitViewManager() const { return UnitViewManager; }

	float GetGridScale() const { return GridScale; }

	/** Ground-plane world location for a discrete cell (Z = 0). */
	FVector GridToWorld(int32 X, int32 Y) const;
	/** Inverse of GridToWorld; clamps to sim world bounds. Returns false if GridScale is invalid. */
	bool WorldToGrid(const FVector& WorldLocation, int32& OutX, int32& OutY) const;

protected:
	/** Unreal units per discrete sim grid point (10 UU = 1 dm per cell → 1000 cells = 10k UU). */
	UPROPERTY(EditDefaultsOnly, Category = "SimRTS")
	float GridScale = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug")
	bool bShowObstructionGrid = false;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug", meta = (ClampMin = "1"))
	int32 ObstructionGridMaxBlueDistance = 30;

	/** Editor-only overlay: dashed arrows for each unit's current and queued move segments. */
	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug")
	bool bShowUnitPaths = true;

	UPROPERTY()
	TObjectPtr<ASimRTSObstructionGridVisualizer> ObstructionGridVisualizer;

	UPROPERTY()
	TObjectPtr<ASimRTSPathVisualizer> PathVisualizer;

	/**
	 * Optional display Blueprints (loaded by soft path).
	 * Defaults target Content/Units/MySimRTS* actors; empty/missing → native C++ class.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Units")
	TSoftClassPtr<ASimRTSUnitActor> SoldierActorClass;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Units")
	TSoftClassPtr<ASimRTSUnitActor> VehicleActorClass;

	void OnSimTick();

private:
	FSimBridge Bridge;
	FTimerHandle SimTimerHandle;

	UPROPERTY()
	TObjectPtr<UUnitViewManager> UnitViewManager;
};
