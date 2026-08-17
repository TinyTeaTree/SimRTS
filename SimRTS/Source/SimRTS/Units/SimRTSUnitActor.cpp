#include "SimRTSUnitActor.h"

#include "UnitDef.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

ASimRTSUnitActor::ASimRTSUnitActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	ConfigureMeshCollision();
}

void ASimRTSUnitActor::BeginPlay()
{
	Super::BeginPlay();
	OnSelectionChanged(false);
}

void ASimRTSUnitActor::ConfigureMeshCollision()
{
	// Query-only: selectable via cursor traces, but do not block the player pawn/camera.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ASimRTSUnitActor::SetupUnit(int32 InUnitId)
{
	UnitId = InUnitId;
}

void ASimRTSUnitActor::ApplyUnitDef(const SimRTS::UnitDef& Def)
{
	(void)Def;
}

float ASimRTSUnitActor::GetPivotHeight() const
{
	return 0.f;
}

void ASimRTSUnitActor::SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving)
{
	const FVector Location(GroundLocation.X, GroundLocation.Y, GetPivotHeight());
	SetActorLocationAndRotation(Location, FRotator(0.f, YawDegrees, 0.f));

	if (bIsMoving != bMoving)
	{
		bMoving = bIsMoving;
		OnMovingChanged(bMoving);
	}
}

void ASimRTSUnitActor::OnMovingChanged(bool bIsMoving)
{
	(void)bIsMoving;
}

void ASimRTSUnitActor::SetSelected(bool bSelected)
{
	OnSelectionChanged(bSelected);
}
