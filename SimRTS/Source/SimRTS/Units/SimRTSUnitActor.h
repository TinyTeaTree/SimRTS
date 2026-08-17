#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimRTSUnitActor.generated.h"

namespace SimRTS
{
struct UnitDef;
}

class USceneComponent;
class UStaticMeshComponent;

/** Base display actor for a sim unit. Subclasses choose mesh / size. */
UCLASS(Abstract)
class SIMRTS_API ASimRTSUnitActor : public AActor
{
	GENERATED_BODY()

public:
	ASimRTSUnitActor();

	void SetupUnit(int32 InUnitId);
	virtual void ApplyUnitDef(const SimRTS::UnitDef& Def);

	/** Actor origin height above ground (Z=0) so the mesh rests on the floor. */
	virtual float GetPivotHeight() const;

	virtual void SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving);

	/** Display-only: this unit was the pinned (infinite-mass) side of an idle-push pair. */
	virtual void NotifyPinnedPush();

	void SetSelected(bool bSelected);

	int32 GetUnitId() const { return UnitId; }

protected:
	virtual void BeginPlay() override;

	/** Subclasses override to react to sim move start/stop (e.g. run anim). */
	virtual void OnMovingChanged(bool bIsMoving);

	void ConfigureMeshCollision();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS")
	int32 UnitId = 0;

	bool bMoving = false;

	/** Show selection halo **/
	UFUNCTION(BlueprintImplementableEvent, Category = "SimRTS")
	void OnSelectionChanged(bool bSelected);

	/** World UU per sim point (matches default GameMode GridScale). */
	static constexpr float PointUU = 10.f;
};
