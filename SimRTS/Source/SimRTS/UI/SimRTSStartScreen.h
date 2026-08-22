#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SimRTSStartScreen.generated.h"

class UButton;
class UTextBlock;

DECLARE_MULTICAST_DELEGATE(FOnSimRTSStartClicked);

/** Pre-room menu. For now: a single Start button that loads the default room. */
UCLASS()
class SIMRTS_API USimRTSStartScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	USimRTSStartScreen(const FObjectInitializer& ObjectInitializer);

	FOnSimRTSStartClicked OnStartClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildTreeIfNeeded();

	UFUNCTION()
	void HandleStartClicked();

	UPROPERTY()
	TObjectPtr<UButton> StartButton;
};
