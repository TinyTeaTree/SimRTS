#include "SimRTSObstructionCylinderVolume.h"

#include "SimRTSObstructionVolume.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ASimRTSObstructionCylinderVolume::ASimRTSObstructionCylinderVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Tags.AddUnique(ASimRTSObstructionVolume::ObstructionTag());

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);
	// BasicShapes Cylinder: radius 50 UU, height 100 UU at scale 1.
	Capsule->SetCapsuleSize(/*Radius=*/50.f, /*HalfHeight=*/50.f);
	// Visual/bake helper only — must not steal cursor traces from floor/units.
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Capsule->SetGenerateOverlapEvents(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
}
