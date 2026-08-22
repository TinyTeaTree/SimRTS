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

ASimRTSGameMode::ASimRTSGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
	PlayerControllerClass = ASimRTSPlayerController::StaticClass();
	HUDClass = ASimRTSDebugHUD::StaticClass();

	Room = CreateDefaultSubobject<USimRTSRoom>(TEXT("Room"));

	// Soft paths: C++ GameMode stays native; Blueprints are loaded at BeginPlay if present.
	SoldierActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSSoldierActor.MySimRTSSoldierActor_C")));
	VehicleActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSVehicleActor.MySimRTSVehicleActor_C")));
}

void ASimRTSGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UUnitViewManager* ViewManager = GetUnitViewManager())
	{
		if (UClass* SoldierClass = SoldierActorClass.LoadSynchronous())
		{
			ViewManager->SetActorClassForType(SimRTS::UnitType::Soldier, SoldierClass);
		}
		if (UClass* VehicleClass = VehicleActorClass.LoadSynchronous())
		{
			ViewManager->SetActorClassForType(SimRTS::UnitType::Vehicle, VehicleClass);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SimRTS GameMode ready. Waiting to load room."));
}

void ASimRTSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Room != nullptr)
	{
		Room->Stop(*this);
	}
	DestroyDebugVisualizers();

	Super::EndPlay(EndPlayReason);
}

bool ASimRTSGameMode::StartDefaultRoom()
{
	if (Room == nullptr)
	{
		return false;
	}

	if (Room->IsLoaded())
	{
		return true;
	}

	if (!Room->LoadDefault(*this))
	{
		UE_LOG(LogTemp, Error, TEXT("SimRTS GameMode: room failed to load; sim will not start."));
		return false;
	}

	SpawnDebugVisualizers();
	return true;
}

void ASimRTSGameMode::SpawnDebugVisualizers()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Room == nullptr)
	{
		return;
	}

	if (bShowObstructionGrid)
	{
		ObstructionGridVisualizer = World->SpawnActor<ASimRTSObstructionGridVisualizer>();
		if (ObstructionGridVisualizer)
		{
			ObstructionGridVisualizer->Build(Room->GetBridge().GetStaticData().pathing, GridScale, ObstructionGridMaxBlueDistance);
		}
	}

	if (bShowUnitPaths)
	{
		PathVisualizer = World->SpawnActor<ASimRTSPathVisualizer>();
	}
}

void ASimRTSGameMode::DestroyDebugVisualizers()
{
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
}

FVector ASimRTSGameMode::GridToWorld(int32 X, int32 Y) const
{
	const float WorldW = static_cast<float>(GetBridge().GetStaticData().world_width) * GridScale;
	const float WorldH = static_cast<float>(GetBridge().GetStaticData().world_height) * GridScale;
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

	const int32 WorldWidth = GetBridge().GetStaticData().world_width;
	const int32 WorldHeight = GetBridge().GetStaticData().world_height;
	const float WorldW = static_cast<float>(WorldWidth) * GridScale;
	const float WorldH = static_cast<float>(WorldHeight) * GridScale;

	const int32 GridX = FMath::FloorToInt((WorldLocation.X + WorldW * 0.5f) / GridScale);
	const int32 GridY = FMath::FloorToInt((WorldLocation.Y + WorldH * 0.5f) / GridScale);

	OutX = FMath::Clamp(GridX, 0, FMath::Max(0, WorldWidth - 1));
	OutY = FMath::Clamp(GridY, 0, FMath::Max(0, WorldHeight - 1));
	return true;
}
