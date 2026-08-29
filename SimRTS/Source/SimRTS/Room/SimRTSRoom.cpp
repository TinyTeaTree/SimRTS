#include "SimRTSRoom.h"

#include "SimRTSGameMode.h"
#include "UnitViewManager.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
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

	bLoaded = true;

	GameMode.RecordGameplayHash();

	UE_LOG(LogTemp, Log, TEXT("SimRTS Room loaded. Units=%d GridScale=%.1f EngineTicksPerSecond=%d (waiting for kickoff)"),
		static_cast<int32>(Bridge.GetState().units.size()),
		GameMode.GetGridScale(),
		Bridge.GetStaticData().ticks_per_second);

	return true;
}

void USimRTSRoom::StartClock(ASimRTSGameMode& GameMode)
{
	if (!bLoaded || bClockStarted)
	{
		return;
	}

	OriginSeconds = FPlatformTime::Seconds();
	bTickHalted = false;
	bClockStarted = true;
	ScheduleNextAttempt(GameMode);

	const int32 TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	UE_LOG(LogTemp, Log, TEXT("SimRTS sim clock started (tps=%d min_delay=%.3fs)"),
		TicksPerSecond,
		GameMode.GetMinTickDelaySeconds());
}

void USimRTSRoom::Stop(ASimRTSGameMode& GameMode)
{
	if (UWorld* World = GameMode.GetWorld())
	{
		World->GetTimerManager().ClearTimer(SimTimerHandle);
	}

	bLoaded = false;
	bClockStarted = false;
	bTickHalted = false;
	OriginSeconds = 0.0;
}

void USimRTSRoom::SetTickHalted(bool bHalted)
{
	if (!bClockStarted || bTickHalted == bHalted)
	{
		return;
	}

	bTickHalted = bHalted;

	ASimRTSGameMode* GameMode = Cast<ASimRTSGameMode>(GetOuter());
	if (GameMode == nullptr)
	{
		return;
	}

	if (bTickHalted)
	{
		if (UWorld* World = GameMode->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SimTimerHandle);
		}
		return;
	}

	ScheduleNextAttempt(*GameMode);
}

int32 USimRTSRoom::GetTicksBehind() const
{
	if (!bClockStarted)
	{
		return 0;
	}

	const double TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	const int32 Expected = FMath::FloorToInt32((FPlatformTime::Seconds() - OriginSeconds) * TicksPerSecond);
	// One tick of slack: late this interval is a shorter next wait, not zoom.
	// Catch-up only if we are still behind after a full 1/tps.
	return FMath::Max(0, Expected - Bridge.GetTick() - 1);
}

int32 USimRTSRoom::GetActualTick() const
{
	if (!bClockStarted)
	{
		return 0;
	}

	const double TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	return FMath::FloorToInt32((FPlatformTime::Seconds() - OriginSeconds) * TicksPerSecond);
}

void USimRTSRoom::OnSimTick()
{
	if (!bLoaded || bTickHalted)
	{
		return;
	}

	ASimRTSGameMode* GameMode = Cast<ASimRTSGameMode>(GetOuter());
	if (GameMode != nullptr && !GameMode->HasAllCommandsForSimTick())
	{
		if (bClockStarted && !bTickHalted)
		{
			ScheduleNextAttempt(*GameMode, true);
		}
		return;
	}

	Bridge.StepForward();
	if (GameMode != nullptr)
	{
		GameMode->RecordGameplayHash();
	}

	if (GameMode != nullptr && UnitViewManager != nullptr)
	{
		UnitViewManager->SyncActors(GameMode);
	}

	if (GameMode != nullptr && bClockStarted && !bTickHalted)
	{
		ScheduleNextAttempt(*GameMode);
	}
}

void USimRTSRoom::ScheduleNextAttempt(ASimRTSGameMode& GameMode, bool bLocked)
{
	UWorld* World = GameMode.GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Wait = bLocked
		? GameMode.GetMinTickDelaySeconds()
		: ComputeWaitSeconds(GameMode.GetMinTickDelaySeconds());
	World->GetTimerManager().SetTimer(
		SimTimerHandle,
		this,
		&USimRTSRoom::OnSimTick,
		Wait,
		false);
}

double USimRTSRoom::ComputeWaitSeconds(double MinTickDelaySeconds) const
{
	const double TicksPerSecond = FMath::Max(1, Bridge.GetStaticData().ticks_per_second);
	const double Wait = OriginSeconds + (Bridge.GetTick() + 1) / TicksPerSecond - FPlatformTime::Seconds();
	if (Wait < MinTickDelaySeconds)
	{
		return MinTickDelaySeconds;
	}
	return Wait;
}
