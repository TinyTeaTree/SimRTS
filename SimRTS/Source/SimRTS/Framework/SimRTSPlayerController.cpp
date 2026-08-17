#include "SimRTSPlayerController.h"

#include "OrderManager.h"
#include "SelectionManager.h"
#include "InputCoreTypes.h"

ASimRTSPlayerController::ASimRTSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void ASimRTSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	SelectionManager = NewObject<USelectionManager>(this);
	OrderManager = NewObject<UOrderManager>(this);

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void ASimRTSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ASimRTSPlayerController::OnLeftPressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ASimRTSPlayerController::OnLeftReleased);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ASimRTSPlayerController::OnRightPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ASimRTSPlayerController::OnRightReleased);
}

void ASimRTSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (LeftGesture.bTracking)
	{
		UpdateGestureDrag(LeftGesture);
	}
	if (RightGesture.bTracking)
	{
		UpdateGestureDrag(RightGesture);
	}
}

void ASimRTSPlayerController::BeginGesture(FSimRTSClickGesture& Gesture)
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		Gesture = {};
		return;
	}

	Gesture.bTracking = true;
	Gesture.bDragged = false;
	Gesture.PressScreenPos = FVector2D(MouseX, MouseY);
	Gesture.PressTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void ASimRTSPlayerController::UpdateGestureDrag(FSimRTSClickGesture& Gesture)
{
	if (!Gesture.bTracking || Gesture.bDragged)
	{
		return;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	const float DistSq = FVector2D::DistSquared(Gesture.PressScreenPos, FVector2D(MouseX, MouseY));
	if (DistSq >= FMath::Square(ClickDragThresholdPixels))
	{
		Gesture.bDragged = true;
	}
}

bool ASimRTSPlayerController::ResolveClick(FSimRTSClickGesture& Gesture)
{
	if (!Gesture.bTracking)
	{
		return false;
	}

	const bool bWasDrag = Gesture.bDragged;
	const double PressTime = Gesture.PressTimeSeconds;
	Gesture = {};

	if (bWasDrag)
	{
		return false;
	}

	const double Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : PressTime;
	if ((Now - PressTime) > MaxClickDurationSeconds)
	{
		return false;
	}

	return true;
}

void ASimRTSPlayerController::OnLeftPressed()
{
	BeginGesture(LeftGesture);
}

void ASimRTSPlayerController::OnLeftReleased()
{
	UpdateGestureDrag(LeftGesture);
	if (ResolveClick(LeftGesture) && SelectionManager != nullptr)
	{
		SelectionManager->HandleLeftClick(this);
	}
}

void ASimRTSPlayerController::OnRightPressed()
{
	BeginGesture(RightGesture);
}

void ASimRTSPlayerController::OnRightReleased()
{
	UpdateGestureDrag(RightGesture);
	if (ResolveClick(RightGesture) && OrderManager != nullptr)
	{
		OrderManager->HandleRightClick(this, SelectionManager);
	}
}
