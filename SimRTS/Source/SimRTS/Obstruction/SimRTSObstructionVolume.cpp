#include "SimRTSObstructionVolume.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

FName ASimRTSObstructionVolume::ObstructionTag()
{
	return FName(TEXT("SimRTS.Obstruction"));
}

ASimRTSObstructionVolume::ASimRTSObstructionVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	//bIsEditorOnlyActor = true;
	SetCanBeDamaged(false);

	Tags.AddUnique(ObstructionTag());

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	// Visual/bake helper only — must not steal cursor traces from floor/units.
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(false);
	//Box->SetHiddenInGame(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	//Mesh->SetHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}
