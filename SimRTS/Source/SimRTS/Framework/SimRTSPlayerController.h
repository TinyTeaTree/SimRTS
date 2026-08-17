#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimRTSPlayerController.generated.h"

class UOrderManager;
class USelectionManager;

/** Tracks press→release so camera drags do not count as select/order clicks. */
USTRUCT()
struct FSimRTSClickGesture
{
	GENERATED_BODY()

	bool bTracking = false;
	bool bDragged = false;
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	double PressTimeSeconds = 0.0;
};

UCLASS()
class SIMRTS_API ASimRTSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASimRTSPlayerController();

	USelectionManager* GetSelectionManager() const { return SelectionManager; }
	UOrderManager* GetOrderManager() const { return OrderManager; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnLeftPressed();
	void OnLeftReleased();
	void OnRightPressed();
	void OnRightReleased();

	void BeginGesture(FSimRTSClickGesture& Gesture);
	void UpdateGestureDrag(FSimRTSClickGesture& Gesture);
	bool ResolveClick(FSimRTSClickGesture& Gesture);

	/** Screen-pixel movement beyond this while held → drag, not click. */
	UPROPERTY(EditAnywhere, Category = "SimRTS|Input")
	float ClickDragThresholdPixels = 6.f;

	/** Releases held longer than this are ignored even if the cursor barely moved. */
	UPROPERTY(EditAnywhere, Category = "SimRTS|Input")
	float MaxClickDurationSeconds = 0.35f;

	FSimRTSClickGesture LeftGesture;
	FSimRTSClickGesture RightGesture;

	UPROPERTY()
	TObjectPtr<USelectionManager> SelectionManager;

	UPROPERTY()
	TObjectPtr<UOrderManager> OrderManager;
};
