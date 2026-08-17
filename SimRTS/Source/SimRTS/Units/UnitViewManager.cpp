#include "UnitViewManager.h"

#include "SimRTSGameMode.h"
#include "SimRTSSoldierActor.h"
#include "SimRTSUnitActor.h"
#include "SimRTSVehicleActor.h"
#include "Engine/World.h"

UUnitViewManager::UUnitViewManager()
{
	ActorClassesByType.Add(static_cast<uint8>(SimRTS::UnitType::Soldier), ASimRTSSoldierActor::StaticClass());
	ActorClassesByType.Add(static_cast<uint8>(SimRTS::UnitType::Vehicle), ASimRTSVehicleActor::StaticClass());
}

void UUnitViewManager::SetActorClassForType(SimRTS::UnitType Type, TSubclassOf<ASimRTSUnitActor> ActorClass)
{
	ActorClassesByType.Add(static_cast<uint8>(Type), ActorClass);
}

TSubclassOf<ASimRTSUnitActor> UUnitViewManager::GetActorClassForType(SimRTS::UnitType Type) const
{
	if (const TSubclassOf<ASimRTSUnitActor>* Found = ActorClassesByType.Find(static_cast<uint8>(Type)))
	{
		return *Found;
	}
	return nullptr;
}

void UUnitViewManager::RebuildActors(ASimRTSGameMode* GameMode)
{
	for (const TPair<int32, TObjectPtr<ASimRTSUnitActor>>& Pair : UnitActors)
	{
		if (ASimRTSUnitActor* Actor = Pair.Value.Get())
		{
			Actor->Destroy();
		}
	}
	UnitActors.Reset();

	if (GameMode == nullptr)
	{
		return;
	}

	UWorld* World = GameMode->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FSimBridge& Bridge = GameMode->GetBridge();
	for (const SimRTS::Unit& Unit : Bridge.GetState().units)
	{
		TSubclassOf<ASimRTSUnitActor> ActorClass = GetActorClassForType(Unit.type);
		if (ActorClass == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnitViewManager: no actor class for unit type %d"),
				static_cast<int32>(Unit.type));
			continue;
		}

		const FVector Ground = GameMode->GridToWorld(Unit.position.x, Unit.position.y);
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ASimRTSUnitActor* Actor = World->SpawnActor<ASimRTSUnitActor>(ActorClass, Ground, FRotator::ZeroRotator, SpawnParams);
		if (Actor == nullptr)
		{
			continue;
		}

		Actor->SetupUnit(Unit.id);
		if (const SimRTS::UnitDef* Def = Bridge.GetStaticData().FindDef(Unit.type))
		{
			Actor->ApplyUnitDef(*Def);
		}

		Actor->SyncWorldPose(Ground, static_cast<float>(Unit.rotation), Unit.move.active);
		UnitActors.Add(Unit.id, Actor);
	}
}

void UUnitViewManager::SyncActors(const ASimRTSGameMode* GameMode) const
{
	if (GameMode == nullptr)
	{
		return;
	}

	for (const SimRTS::Unit& Unit : GameMode->GetBridge().GetState().units)
	{
		if (const TObjectPtr<ASimRTSUnitActor>* Found = UnitActors.Find(Unit.id))
		{
			if (ASimRTSUnitActor* Actor = Found->Get())
			{
				const FVector Ground = GameMode->GridToWorld(Unit.position.x, Unit.position.y);
				Actor->SyncWorldPose(Ground, static_cast<float>(Unit.rotation), Unit.move.active);
			}
		}
	}
}

ASimRTSUnitActor* UUnitViewManager::FindActor(int32 UnitId) const
{
	if (const TObjectPtr<ASimRTSUnitActor>* Found = UnitActors.Find(UnitId))
	{
		return Found->Get();
	}
	return nullptr;
}
