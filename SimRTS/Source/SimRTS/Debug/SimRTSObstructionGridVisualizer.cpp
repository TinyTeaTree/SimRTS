// Fill out your copyright notice in the Description page of Project Settings.


#include "Debug/SimRTSObstructionGridVisualizer.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

// Sets default values
ASimRTSObstructionGridVisualizer::ASimRTSObstructionGridVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;
	
	PlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaneMesh"));
	SetRootComponent(PlaneMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneAsset.Succeeded())
	{
		PlaneMesh->SetStaticMesh(PlaneAsset.Object);
	}

	PlaneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaneMesh->SetCastShadow(false);

}

void ASimRTSObstructionGridVisualizer::Build(const SimRTS::PathingGrid& Pathing, float GridScale, int32 MaxBlueDistance)
{
	int32 w = Pathing.width;
	int32 h = Pathing.height;

	if (w <= 0 || h <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No width or height in map, error"));
		return;
	}

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Debug/M_ObstructionGrid.M_ObstructionGrid"));
	if (BaseMat == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load material base"));
		return;
	}

	UMaterialInstanceDynamic* Mid = PlaneMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMat);
	if (Mid == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create material instance"));
		return;
	}
	
	GridTexture = UTexture2D::CreateTransient(Pathing.width, Pathing.height, PF_B8G8R8A8);
	GridTexture->Filter = TF_Nearest;
	GridTexture->LODGroup = TEXTUREGROUP_Pixels2D;
	GridTexture->MipGenSettings = TMGS_NoMipmaps;
	GridTexture->SRGB = true;
	GridTexture->CompressionSettings = TC_VectorDisplacementmap;
	GridTexture->AddressX = TA_Clamp;
	GridTexture->AddressY = TA_Clamp;

	FTexturePlatformData* PlatformData = GridTexture->GetPlatformData();
	if (PlatformData == nullptr || PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No PlatformData in map, error"));
		return;
	}
	FTexture2DMipMap& Mip0 = PlatformData->Mips[0];
	void* Raw = Mip0.BulkData.Lock(LOCK_READ_WRITE);
	FColor* Pixels = static_cast<FColor*>(Raw);

	for (int32 y = 0; y < h; y++)
	{
		for (int32 x = 0; x < w; x++)
		{
			auto node = Pathing.At(x, y);
			int32 Distance;
			if (node.blocked)
				Distance = 0;
			else
				Distance = node.obstruction_distance;

			float t = FMath::Clamp(Distance / float(MaxBlueDistance), 0.f, 1.f);
			// Hue sweep red -> yellow -> green -> cyan -> blue. MakeFromHSV8 maps 255 to 360 degrees,
			// so blue (240 degrees) is 170.
			uint8 Hue = static_cast<uint8>(t * 170.f);
			FColor Color = FLinearColor::MakeFromHSV8(Hue, 255, 255).ToFColor(true);

			Pixels[y * w + x] = Color;
			
		}
	}

	Mip0.BulkData.Unlock();
	GridTexture->UpdateResource();

	Mid->SetTextureParameterValue(TEXT("GridTexture"), GridTexture);
	Mid->SetScalarParameterValue(TEXT("GridWidth"), static_cast<float>(w));
	Mid->SetScalarParameterValue(TEXT("GridHeight"), static_cast<float>(h));

	const float WorldW = static_cast<float>(w) * GridScale;
	const float WorldH = static_cast<float>(h) * GridScale;
	// 100 UU mesh → map size
	const float ScaleX = WorldW / 100.f;
	const float ScaleY = WorldH / 100.f;
	SetActorLocation(FVector(0.f, 0.f, 1.f));
	SetActorScale3D(FVector(ScaleX, ScaleY, 1.f));
}
