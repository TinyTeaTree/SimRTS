#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimRTSCommsView.h"
#include "SimRTSPlayerController.generated.h"

class UOrderManager;
class USelectionManager;
class USimRTSMainMenu;

enum class ESimRTSMenuPage : uint8
{
	Login,
	Lobby,
	Room
};

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

	void ShowMainMenu();
	void HideMainMenu();
	void ApplyGameplayInputMode();

	void HandleLoginRequested(const FString& Username);
	void HandleCreateRoomRequested(const FString& RoomId);
	void HandleJoinRoomRequested(const FString& RoomId);
	void HandleLeaveRequested();
	void HandleStartClicked();
	void HandleCommsEvent(const FSimRTSCommsEventView& Event);

	void BeginGesture(FSimRTSClickGesture& Gesture);
	void UpdateGestureDrag(FSimRTSClickGesture& Gesture);
	bool ResolveClick(FSimRTSClickGesture& Gesture);

	UPROPERTY(EditAnywhere, Category = "SimRTS|Input")
	float ClickDragThresholdPixels = 6.f;

	UPROPERTY(EditAnywhere, Category = "SimRTS|Input")
	float MaxClickDurationSeconds = 0.35f;

	FSimRTSClickGesture LeftGesture;
	FSimRTSClickGesture RightGesture;

	UPROPERTY()
	TObjectPtr<USimRTSMainMenu> MainMenu;

	UPROPERTY()
	TObjectPtr<USelectionManager> SelectionManager;

	UPROPERTY()
	TObjectPtr<UOrderManager> OrderManager;

	ESimRTSMenuPage MenuPage = ESimRTSMenuPage::Login;
	TArray<FSimRTSCommsRoomView> LastRooms;
};
