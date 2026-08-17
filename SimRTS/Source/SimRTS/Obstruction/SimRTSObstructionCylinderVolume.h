#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimRTSObstructionCylinderVolume.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

/** Editor-authored round (cylindrical) obstruction. Baked as a circular XY footprint. */
UCLASS(ClassGroup = SimRTS, meta = (DisplayName = "SimRTS Obstruction Cylinder"))
class SIMRTS_API ASimRTSObstructionCylinderVolume : public AActor
{
	GENERATED_BODY()

public:
	ASimRTSObstructionCylinderVolume();

	UCapsuleComponent* GetCapsule() const { return Capsule; }

protected:
	/** Query volume for bake tests. Radius 50 / half-height 50 → matches BasicShapes cylinder at scale 1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Obstruction")
	TObjectPtr<UCapsuleComponent> Capsule;

	/** Viewport preview only; no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Obstruction")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
