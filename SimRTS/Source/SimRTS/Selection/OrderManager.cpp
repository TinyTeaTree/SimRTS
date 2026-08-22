#include "OrderManager.h"

#include "SelectionManager.h"
#include "SimRTSGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

namespace
{
	/** Cursor ray ∩ ground plane Z=0 — ignores all world collision. */
	bool CursorToGround(APlayerController* PlayerController, FVector& OutGroundLocation)
	{
		if (PlayerController == nullptr)
		{
			return false;
		}

		FVector WorldOrigin = FVector::ZeroVector;
		FVector WorldDirection = FVector::ZeroVector;
		if (!PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
		{
			return false;
		}

		if (FMath::Abs(WorldDirection.Z) <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float T = -WorldOrigin.Z / WorldDirection.Z;
		if (T < 0.f)
		{
			return false;
		}

		OutGroundLocation = WorldOrigin + WorldDirection * T;
		return true;
	}
}

void UOrderManager::HandleRightClick(APlayerController* PlayerController, const USelectionManager* SelectionManager)
{
	if (PlayerController == nullptr || SelectionManager == nullptr)
	{
		return;
	}

	const TArray<int32>& SelectedIds = SelectionManager->GetSelection().GetSelectedUnitIds();
	if (SelectedIds.Num() == 0)
	{
		return;
	}

	FVector GroundLocation = FVector::ZeroVector;
	if (!CursorToGround(PlayerController, GroundLocation))
	{
		return;
	}

	UWorld* World = PlayerController->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	ASimRTSGameMode* GameMode = World->GetAuthGameMode<ASimRTSGameMode>();
	if (GameMode == nullptr || !GameMode->IsRoomLoaded())
	{
		return;
	}

	int32 GridX = 0;
	int32 GridY = 0;
	if (!GameMode->WorldToGrid(GroundLocation, GridX, GridY))
	{
		return;
	}

	const bool bIsNext = PlayerController->IsInputKeyDown(EKeys::LeftShift)
		|| PlayerController->IsInputKeyDown(EKeys::RightShift);

	GameMode->GetBridge().SubmitMoveOrder(SelectedIds, GridX, GridY, bIsNext);
}
