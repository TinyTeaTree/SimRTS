// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BattleState.h"
#include "GameFramework/Actor.h"
#include "SimRTSObstructionGridVisualizer.generated.h"

UCLASS()
class SIMRTS_API ASimRTSObstructionGridVisualizer : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASimRTSObstructionGridVisualizer();

	void Build(const SimRTS::PathingGrid& Pathing, float GridScale, int32 MaxBlueDistance);


protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UTexture2D> GridTexture;
};
