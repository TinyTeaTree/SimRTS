#include "SimRTSSpawnMarker.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace SimRTSSpawnMarkerPrivate
{
	constexpr float kBasicCylinderHalfHeightUU = 50.f;
	constexpr float kBasicCubeHalfExtentUU = 50.f;
	constexpr float kSoldierScaleZ = 2.f;
}

FName ASimRTSSpawnMarker::SpawnTag()
{
	return FName(TEXT("SimRTS.Spawn"));
}

ASimRTSSpawnMarker::ASimRTSSpawnMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
	SetCanBeDamaged(false);
	SetActorHiddenInGame(true);

	Tags.AddUnique(SpawnTag());

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetHiddenInGame(true);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SceneRoot);
	FacingArrow->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	FacingArrow->SetUsingAbsoluteScale(true);
	FacingArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FacingArrow->SetCastShadow(false);
	FacingArrow->SetHiddenInGame(true);
	FacingArrow->ArrowColor = FColor(80, 200, 255);
	FacingArrow->ArrowSize = 1.5f;
	FacingArrow->bIsScreenSizeScaled = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		SoldierMesh = CylinderMesh.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		VehicleMesh = CubeMesh.Object;
	}

	ApplyPreviewVisuals();
}

FString ASimRTSSpawnMarker::GetUnitTypeJsonName() const
{
	switch (UnitType)
	{
	case ESimRTSSpawnUnitType::Vehicle:
		return TEXT("Vehicle");
	case ESimRTSSpawnUnitType::Soldier:
	default:
		return TEXT("Soldier");
	}
}

void ASimRTSSpawnMarker::RefreshPreview()
{
	ApplyPreviewVisuals();
}

void ASimRTSSpawnMarker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPreviewVisuals();
}

#if WITH_EDITOR
void ASimRTSSpawnMarker::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(ASimRTSSpawnMarker, UnitType)
		|| PropName == NAME_None)
	{
		ApplyPreviewVisuals();
	}
}
#endif

void ASimRTSSpawnMarker::ApplyPreviewVisuals()
{
	if (Mesh == nullptr)
	{
		return;
	}

	const bool bVehicle = UnitType == ESimRTSSpawnUnitType::Vehicle;
	UStaticMesh* PreviewMesh = bVehicle ? VehicleMesh.Get() : SoldierMesh.Get();
	if (PreviewMesh != nullptr && Mesh->GetStaticMesh() != PreviewMesh)
	{
		Mesh->SetStaticMesh(PreviewMesh);
	}

	if (bVehicle)
	{
		Mesh->SetRelativeScale3D(FVector(1.f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, SimRTSSpawnMarkerPrivate::kBasicCubeHalfExtentUU));
		if (FacingArrow != nullptr)
		{
			FacingArrow->ArrowColor = FColor(255, 160, 60);
		}
	}
	else
	{
		Mesh->SetRelativeScale3D(FVector(1.f, 1.f, SimRTSSpawnMarkerPrivate::kSoldierScaleZ));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, SimRTSSpawnMarkerPrivate::kBasicCylinderHalfHeightUU * SimRTSSpawnMarkerPrivate::kSoldierScaleZ));
		if (FacingArrow != nullptr)
		{
			FacingArrow->ArrowColor = FColor(80, 200, 255);
		}
	}
}
