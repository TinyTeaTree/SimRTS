#include "SimRTSVehicleActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Basic cube is 100 UU (1 m) on a side at scale 1.
	constexpr float kBasicCubeHalfExtent = 50.f;
}

ASimRTSVehicleActor::ASimRTSVehicleActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}

	// Hide the cube; selection moves to TankMesh.
	MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, kBasicCubeHalfExtent));
	MeshComponent->SetHiddenInGame(true);
	MeshComponent->SetVisibility(false);
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TankMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TankMesh"));
	TankMesh->SetupAttachment(SceneRoot);
	TankMesh->SetGenerateOverlapEvents(false);
	TankMesh->SetHiddenInGame(false);
	TankMesh->SetVisibility(true);
	ConfigureTankCollision();

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> TankFinder(
		TEXT("/Game/StylizedTank/Mesh/DualBarrel/SK_StylizedTank_DualBarrel.SK_StylizedTank_DualBarrel"));
	if (TankFinder.Succeeded())
	{
		TankSkeletalMesh = TankFinder.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("ASimRTSVehicleActor: failed to load SK_StylizedTank_DualBarrel — open the editor once to cook/load Content/StylizedTank"));
	}

	ApplyTankDefaults();
}

void ASimRTSVehicleActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyTankDefaults();
}

void ASimRTSVehicleActor::ConfigureTankCollision()
{
	if (TankMesh == nullptr)
	{
		return;
	}

	// Query-only: selectable via cursor traces, does not block pawns/camera.
	TankMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TankMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	TankMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ASimRTSVehicleActor::ApplyTankDefaults()
{
	if (TankMesh == nullptr)
	{
		return;
	}

	// Absolute world pose only while smoothing; otherwise follow the actor.
	TankMesh->SetUsingAbsoluteLocation(bSmoothVisualPose);
	TankMesh->SetUsingAbsoluteRotation(bSmoothVisualPose);
	if (!bSmoothVisualPose)
	{
		TankMesh->SetRelativeLocation(FVector::ZeroVector);
		TankMesh->SetRelativeRotation(FRotator(0.f, MeshYawOffsetDegrees, 0.f));
	}

	if (TankSkeletalMesh != nullptr && TankMesh->GetSkeletalMeshAsset() != TankSkeletalMesh)
	{
		TankMesh->SetSkeletalMesh(TankSkeletalMesh);
	}

	TankMesh->SetVisibility(true);
	TankMesh->SetHiddenInGame(false);
	ConfigureTankCollision();

	// Cube must never steal clicks.
	if (MeshComponent != nullptr)
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetHiddenInGame(true);
		MeshComponent->SetVisibility(false);
	}
}

void ASimRTSVehicleActor::BeginPlay()
{
	Super::BeginPlay();
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
	ApplyTankDefaults();
}

void ASimRTSVehicleActor::SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving)
{
	// Actor / arrow stay on the discrete sim pose.
	Super::SyncWorldPose(GroundLocation, YawDegrees, bIsMoving);

	VisualTargetLocation = GetActorLocation();
	VisualTargetYawDegrees = YawDegrees;

	if (TankMesh != nullptr)
	{
		TankMesh->SetUsingAbsoluteLocation(bSmoothVisualPose);
		TankMesh->SetUsingAbsoluteRotation(bSmoothVisualPose);
	}

	if (!bVisualPoseInitialized || !bSmoothVisualPose)
	{
		SnapVisualToTarget();
		bVisualPoseInitialized = true;
	}
}

void ASimRTSVehicleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSmoothVisualPose || !bVisualPoseInitialized || TankMesh == nullptr)
	{
		return;
	}

	const FVector NewLocation = FMath::VInterpTo(
		TankMesh->GetComponentLocation(),
		VisualTargetLocation,
		DeltaSeconds,
		VisualLocationInterpSpeed);
	TankMesh->SetWorldLocation(NewLocation);

	const FRotator CurrentRot(0.f, TankMesh->GetComponentRotation().Yaw, 0.f);
	const FRotator TargetRot(0.f, VisualTargetYawDegrees + MeshYawOffsetDegrees, 0.f);
	TankMesh->SetWorldRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, VisualRotationInterpSpeed));
}

void ASimRTSVehicleActor::SnapVisualToTarget()
{
	if (TankMesh == nullptr)
	{
		return;
	}

	if (bSmoothVisualPose)
	{
		TankMesh->SetWorldLocation(VisualTargetLocation);
		TankMesh->SetWorldRotation(FRotator(0.f, VisualTargetYawDegrees + MeshYawOffsetDegrees, 0.f));
	}
	else
	{
		TankMesh->SetRelativeLocation(FVector::ZeroVector);
		TankMesh->SetRelativeRotation(FRotator(0.f, MeshYawOffsetDegrees, 0.f));
	}
}

float ASimRTSVehicleActor::GetPivotHeight() const
{
	return 0.f;
}
