#include "SelectionManager.h"

#include "SimRTSGameMode.h"
#include "SimRTSUnitActor.h"
#include "UnitViewManager.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void USelectionManager::HandleLeftClick(APlayerController* PlayerController)
{
	if (PlayerController == nullptr)
	{
		return;
	}

	const TArray<int32> PreviousUnitIds = Selection.GetSelectedUnitIds();
	const bool bShift =
		PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);

	UWorld* World = PlayerController->GetWorld();
	if (World == nullptr)
	{
		if (!bShift)
		{
			Selection.Clear();
			ApplySelectionVisuals(PlayerController, PreviousUnitIds);
		}
		return;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		if (!bShift)
		{
			Selection.Clear();
			ApplySelectionVisuals(PlayerController, PreviousUnitIds);
		}
		return;
	}

	const FVector TraceStart = WorldOrigin;
	const FVector TraceEnd = WorldOrigin + WorldDirection * 100000.f;

	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimRTSSelection), false);
	World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, ECC_Visibility, Params);

	for (const FHitResult& Hit : Hits)
	{
		if (ASimRTSUnitActor* UnitActor = Cast<ASimRTSUnitActor>(Hit.GetActor()))
		{
			if (bShift)
			{
				Selection.Add(UnitActor->GetUnitId());
			}
			else
			{
				Selection.Select(UnitActor->GetUnitId());
			}
			ApplySelectionVisuals(PlayerController, PreviousUnitIds);
			return;
		}
	}

	// Miss: plain click clears; shift-click keeps current selection.
	if (!bShift)
	{
		Selection.Clear();
		ApplySelectionVisuals(PlayerController, PreviousUnitIds);
	}
}

void USelectionManager::ApplySelectionVisuals(APlayerController* PlayerController, const TArray<int32>& PreviousUnitIds)
{
	if (PlayerController == nullptr)
	{
		return;
	}

	UWorld* World = PlayerController->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const ASimRTSGameMode* GameMode = World->GetAuthGameMode<ASimRTSGameMode>();
	if (GameMode == nullptr)
	{
		return;
	}

	UUnitViewManager* ViewManager = GameMode->GetUnitViewManager();
	if (ViewManager == nullptr)
	{
		return;
	}

	const TArray<int32>& NewUnitIds = Selection.GetSelectedUnitIds();

	for (const int32 UnitId : PreviousUnitIds)
	{
		if (!NewUnitIds.Contains(UnitId))
		{
			if (ASimRTSUnitActor* Actor = ViewManager->FindActor(UnitId))
			{
				Actor->SetSelected(false);
			}
		}
	}

	for (const int32 UnitId : NewUnitIds)
	{
		if (ASimRTSUnitActor* Actor = ViewManager->FindActor(UnitId))
		{
			Actor->SetSelected(true);
		}
	}
}
