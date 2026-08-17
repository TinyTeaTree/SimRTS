#pragma once

#include "CoreMinimal.h"
#include "SimRTSUnitActor.h"
#include "SimRTSSoldierActor.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Soldier display — Epic UE5 Manny skeletal mesh + jog/idle.
 * Cylinder stays on discrete sim pose; character mesh smooth-follows it.
 */
UCLASS()
class SIMRTS_API ASimRTSSoldierActor : public ASimRTSUnitActor
{
	GENERATED_BODY()

public:
	ASimRTSSoldierActor();

	virtual float GetPivotHeight() const override;
	virtual void SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving) override;
	virtual void NotifyPinnedPush() override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnMovingChanged(bool bIsMoving) override;

	void ApplyCharacterDefaults();
	void PlayLocomotion(bool bIsMoving);
	void PlayPushedAnim();
	void OnPushAnimFinished();
	void SnapVisualToTarget();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS")
	TObjectPtr<USkeletalMeshComponent> CharacterMesh;

	/** UE5 Manny mesh faces +Y; actor/arrow forward is +X → yaw -90. */
	UPROPERTY(EditAnywhere, Category = "SimRTS|Character")
	float MeshYawOffsetDegrees = -90.f;

	/** If true, character smooth-follows the cylinder; if false, snaps with the sim tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimRTS|Character")
	bool bSmoothVisualPose = true;

	/** How quickly the character chases the cylinder (higher = snappier). */
	UPROPERTY(EditAnywhere, Category = "SimRTS|Character", meta = (ClampMin = "0.1", EditCondition = "bSmoothVisualPose"))
	float VisualLocationInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "SimRTS|Character", meta = (ClampMin = "0.1", EditCondition = "bSmoothVisualPose"))
	float VisualRotationInterpSpeed = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Character")
	TObjectPtr<USkeletalMesh> MannequinMesh;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Character")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Character")
	TObjectPtr<UAnimSequence> RunAnim;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Character")
	TObjectPtr<UAnimSequence> PushedAnim;

private:
	FVector VisualTargetLocation = FVector::ZeroVector;
	float VisualTargetYawDegrees = 0.f;
	bool bVisualPoseInitialized = false;
	bool bPushAnimLocked = false;
	FTimerHandle PushAnimTimer;
};
