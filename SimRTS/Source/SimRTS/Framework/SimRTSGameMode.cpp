#include "SimRTSGameMode.h"

#include "SimRTSDebugHUD.h"
#include "SimRTSObstructionGridVisualizer.h"
#include "SimRTSPathVisualizer.h"
#include "SimRTSPlayerController.h"
#include "SimRTSUnitActor.h"
#include "Types.h"
#include "UnitViewManager.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "TimerManager.h"

ASimRTSGameMode::ASimRTSGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
	PlayerControllerClass = ASimRTSPlayerController::StaticClass();
	HUDClass = ASimRTSDebugHUD::StaticClass();

	// Soft paths: C++ GameMode stays native; Blueprints are loaded at BeginPlay if present.
	SoldierActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSSoldierActor.MySimRTSSoldierActor_C")));
	VehicleActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSVehicleActor.MySimRTSVehicleActor_C")));
}

void ASimRTSGameMode::BeginPlay()
{
	Super::BeginPlay();

	UnitViewManager = NewObject<UUnitViewManager>(this);
	if (UClass* SoldierClass = SoldierActorClass.LoadSynchronous())
	{
		UnitViewManager->SetActorClassForType(SimRTS::UnitType::Soldier, SoldierClass);
	}
	if (UClass* VehicleClass = VehicleActorClass.LoadSynchronous())
	{
		UnitViewManager->SetActorClassForType(SimRTS::UnitType::Vehicle, VehicleClass);
	}

	if (!Bridge.ResetToDefaultLevel())
	{
		UE_LOG(LogTemp, Error, TEXT("SimRTS GameMode: level JSON failed to load; sim will not start."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (bShowObstructionGrid)
		{
			ObstructionGridVisualizer = World->SpawnActor<ASimRTSObstructionGridVisualizer>();
			if (ObstructionGridVisualizer)
			{
				ObstructionGridVisualizer->Build(Bridge.GetStaticData().pathing, GridScale, ObstructionGridMaxBlueDistance);
			}
		}

		if (bShowUnitPaths)
		{
			PathVisualizer = World->SpawnActor<ASimRTSPathVisualizer>();
		}
	}

	
	UnitViewManager->RebuildActors(this);
	UnitViewManager->SyncActors(this);

	const int32 TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	const float SimTickInterval = 1.f / static_cast<float>(TicksPerSecond);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SimTimerHandle,
			this,
			&ASimRTSGameMode::OnSimTick,
			SimTickInterval,
			true);
	}

	UE_LOG(LogTemp, Log, TEXT("SimRTS GameMode started. Units=%d GridScale=%.1f EngineTicksPerSecond=%d (interval=%.3fs)"),
		static_cast<int32>(Bridge.GetState().units.size()),
		GridScale,
		TicksPerSecond,
		SimTickInterval);
}

void ASimRTSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimTimerHandle);
	}

	if (ObstructionGridVisualizer)
	{
		ObstructionGridVisualizer->Destroy();
		ObstructionGridVisualizer = nullptr;
	}

	if (PathVisualizer)
	{
		PathVisualizer->Destroy();
		PathVisualizer = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ASimRTSGameMode::OnSimTick()
{
	Bridge.StepForward();
	if (UnitViewManager != nullptr)
	{
		UnitViewManager->SyncActors(this);
	}
}

FVector ASimRTSGameMode::GridToWorld(int32 X, int32 Y) const
{
	const float WorldW = static_cast<float>(Bridge.GetStaticData().world_width) * GridScale;
	const float WorldH = static_cast<float>(Bridge.GetStaticData().world_height) * GridScale;
	const float XWorld = static_cast<float>(X) * GridScale - WorldW * 0.5f;
	const float YWorld = static_cast<float>(Y) * GridScale - WorldH * 0.5f;
	return FVector(XWorld, YWorld, 0.f);
}

bool ASimRTSGameMode::WorldToGrid(const FVector& WorldLocation, int32& OutX, int32& OutY) const
{
	if (GridScale <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const int32 WorldWidth = Bridge.GetStaticData().world_width;
	const int32 WorldHeight = Bridge.GetStaticData().world_height;
	const float WorldW = static_cast<float>(WorldWidth) * GridScale;
	const float WorldH = static_cast<float>(WorldHeight) * GridScale;

	const int32 GridX = FMath::FloorToInt((WorldLocation.X + WorldW * 0.5f) / GridScale);
	const int32 GridY = FMath::FloorToInt((WorldLocation.Y + WorldH * 0.5f) / GridScale);

	OutX = FMath::Clamp(GridX, 0, FMath::Max(0, WorldWidth - 1));
	OutY = FMath::Clamp(GridY, 0, FMath::Max(0, WorldHeight - 1));
	return true;
}
