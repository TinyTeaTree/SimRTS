#include "SimRTSRoom.h"

#include "SimRTSGameMode.h"
#include "UnitViewManager.h"
#include "Engine/World.h"
#include "TimerManager.h"

USimRTSRoom::USimRTSRoom()
{
	UnitViewManager = CreateDefaultSubobject<UUnitViewManager>(TEXT("UnitViewManager"));
}

bool USimRTSRoom::LoadDefault(ASimRTSGameMode& GameMode)
{
	if (bLoaded)
	{
		return true;
	}

	if (!Bridge.ResetToDefaultLevel())
	{
		return false;
	}

	if (UnitViewManager != nullptr)
	{
		UnitViewManager->RebuildActors(&GameMode);
		UnitViewManager->SyncActors(&GameMode);
	}

	const int32 TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	const float SimTickInterval = 1.f / static_cast<float>(TicksPerSecond);

	if (UWorld* World = GameMode.GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SimTimerHandle,
			this,
			&USimRTSRoom::OnSimTick,
			SimTickInterval,
			true);
	}

	bLoaded = true;

	UE_LOG(LogTemp, Log, TEXT("SimRTS Room loaded. Units=%d GridScale=%.1f EngineTicksPerSecond=%d (interval=%.3fs)"),
		static_cast<int32>(Bridge.GetState().units.size()),
		GameMode.GetGridScale(),
		TicksPerSecond,
		SimTickInterval);

	return true;
}

void USimRTSRoom::Stop(ASimRTSGameMode& GameMode)
{
	if (UWorld* World = GameMode.GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimTimerHandle);
	}

	bLoaded = false;
}

void USimRTSRoom::OnSimTick()
{
	if (!bLoaded)
	{
		return;
	}

	Bridge.StepForward();

	ASimRTSGameMode* GameMode = Cast<ASimRTSGameMode>(GetOuter());
	if (GameMode != nullptr && UnitViewManager != nullptr)
	{
		UnitViewManager->SyncActors(GameMode);
	}
}
