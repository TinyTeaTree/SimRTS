#pragma once

#include "CoreMinimal.h"
#include "SimRTSUnitActor.h"
#include "SimRTSVehicleActor.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Vehicle display — stylized tank skeletal mesh.
 * Cube stays as a hidden placeholder; tank mesh smooth-follows the discrete sim pose
 * and owns selection (Visibility) collision.
 */
UCLASS()
class SIMRTS_API ASimRTSVehicleActor : public ASimRTSUnitActor
{
	GENERATED_BODY()

public:
	ASimRTSVehicleActor();

	virtual float GetPivotHeight() const override;
	virtual void SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void ApplyTankDefaults();
	void ConfigureTankCollision();
	void SnapVisualToTarget();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS")
	TObjectPtr<USkeletalMeshComponent> TankMesh;

	/** Mesh forward vs actor/arrow +X. Adjust in editor if the tank faces the wrong way. */
	UPROPERTY(EditAnywhere, Category = "SimRTS|Tank")
	float MeshYawOffsetDegrees = 0.f;

	/** If true, tank smooth-follows the actor; if false, snaps with the sim tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimRTS|Tank")
	bool bSmoothVisualPose = true;

	UPROPERTY(EditAnywhere, Category = "SimRTS|Tank", meta = (ClampMin = "0.1", EditCondition = "bSmoothVisualPose"))
	float VisualLocationInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "SimRTS|Tank", meta = (ClampMin = "0.1", EditCondition = "bSmoothVisualPose"))
	float VisualRotationInterpSpeed = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Tank")
	TObjectPtr<USkeletalMesh> TankSkeletalMesh;

private:
	FVector VisualTargetLocation = FVector::ZeroVector;
	float VisualTargetYawDegrees = 0.f;
	bool bVisualPoseInitialized = false;
};
