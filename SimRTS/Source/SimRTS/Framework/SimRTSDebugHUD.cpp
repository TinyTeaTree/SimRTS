#include "SimRTSDebugHUD.h"

#include "SimRTSGameMode.h"
#include "SimRTSPlayerController.h"
#include "SelectionManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

void ASimRTSDebugHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	const ASimRTSGameMode* GameMode = GetWorld()->GetAuthGameMode<ASimRTSGameMode>();
	if (GameMode == nullptr || !GameMode->IsRoomLoaded())
	{
		return;
	}

	const FSimBridge& Bridge = GameMode->GetBridge();
	const int32 Tick = Bridge.GetTick();
	const int32 Units = static_cast<int32>(Bridge.GetState().units.size());

	FString SelectionText = TEXT("Selection: none");
	if (const ASimRTSPlayerController* PC = Cast<ASimRTSPlayerController>(GetOwningPlayerController()))
	{
		if (const USelectionManager* SelectionMgr = PC->GetSelectionManager())
		{
			const TArray<int32>& SelectedIds = SelectionMgr->GetSelection().GetSelectedUnitIds();
			if (SelectedIds.Num() > 0)
			{
				TArray<FString> IdStrings;
				IdStrings.Reserve(SelectedIds.Num());
				for (const int32 Id : SelectedIds)
				{
					IdStrings.Add(FString::FromInt(Id));
				}
				SelectionText = FString::Printf(TEXT("Selection: %s"), *FString::Join(IdStrings, TEXT(", ")));
			}
		}
	}

	UFont* Font = GEngine != nullptr ? GEngine->GetMediumFont() : nullptr;
	const float Scale = 1.4f;
	const float X = 16.f;
	float Y = 16.f;
	const float LineHeight = 22.f * Scale;

	DrawText(FString::Printf(TEXT("Tick: %d"), Tick), FLinearColor::White, X, Y, Font, Scale);
	Y += LineHeight;
	DrawText(FString::Printf(TEXT("Units: %d"), Units), FLinearColor::White, X, Y, Font, Scale);
	Y += LineHeight;
	const int32 RttMs = GameMode->GetMinRttMs();
	if (RttMs < 0)
	{
		DrawText(TEXT("RTT: --"), FLinearColor::White, X, Y, Font, Scale);
	}
	else
	{
		DrawText(FString::Printf(TEXT("RTT: %d ms"), RttMs), FLinearColor::White, X, Y, Font, Scale);
	}
	Y += LineHeight;
	DrawText(SelectionText, FLinearColor::White, X, Y, Font, Scale);
}
