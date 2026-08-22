#include "SimRTSPlayerController.h"

#include "OrderManager.h"
#include "SelectionManager.h"
#include "SimRTSGameMode.h"
#include "SimRTSMainMenu.h"
#include "InputCoreTypes.h"
#include "Misc/Char.h"

namespace {

bool IsValidCommsName(const FString& Name)
{
	if (Name.Len() < 1 || Name.Len() > 64)
	{
		return false;
	}

	for (const TCHAR Character : Name)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

} // namespace

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

	ShowMainMenu();
}

void ASimRTSPlayerController::ShowMainMenu()
{
	if (MainMenu == nullptr)
	{
		MainMenu = CreateWidget<USimRTSMainMenu>(this);
		if (MainMenu == nullptr)
		{
			return;
		}
		MainMenu->OnLoginRequested.AddUObject(this, &ASimRTSPlayerController::HandleLoginRequested);
		MainMenu->OnCreateRoomRequested.AddUObject(this, &ASimRTSPlayerController::HandleCreateRoomRequested);
		MainMenu->OnJoinRoomRequested.AddUObject(this, &ASimRTSPlayerController::HandleJoinRoomRequested);
		MainMenu->OnLeaveRequested.AddUObject(this, &ASimRTSPlayerController::HandleLeaveRequested);
		MainMenu->OnStartClicked.AddUObject(this, &ASimRTSPlayerController::HandleStartClicked);
	}

	if (ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr)
	{
		GameMode->OnCommsEvent.AddUObject(this, &ASimRTSPlayerController::HandleCommsEvent);
		GameMode->SetMatchmakingMenuOpen(true);
	}

	MainMenu->AddToViewport(100);
	MainMenu->ShowLogin();
	MainMenu->SetStatus(TEXT("Login to continue."), false);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void ASimRTSPlayerController::HideMainMenu()
{
	if (ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr)
	{
		GameMode->SetMatchmakingMenuOpen(false);
		GameMode->OnCommsEvent.RemoveAll(this);
	}

	if (MainMenu != nullptr)
	{
		MainMenu->RemoveFromParent();
		MainMenu = nullptr;
	}

	ApplyGameplayInputMode();
}

void ASimRTSPlayerController::ApplyGameplayInputMode()
{
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void ASimRTSPlayerController::HandleLoginRequested(const FString& Username)
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (GameMode == nullptr || MainMenu == nullptr)
	{
		return;
	}

	if (!IsValidCommsName(Username))
	{
		MainMenu->SetStatus(TEXT("Username: 1-64 letters, digits, _ or -."), true);
		return;
	}

	MainMenu->SetBusy(true);
	MainMenu->SetStatus(TEXT("Logging in..."), false);
	GameMode->RequestLogin(Username);
}

void ASimRTSPlayerController::HandleCreateRoomRequested(const FString& RoomId)
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (GameMode == nullptr || MainMenu == nullptr)
	{
		return;
	}

	if (!IsValidCommsName(RoomId))
	{
		MainMenu->SetStatus(TEXT("Room name: 1-64 letters, digits, _ or -."), true);
		return;
	}

	MainMenu->SetBusy(true);
	MainMenu->SetStatus(FString::Printf(TEXT("Creating %s..."), *RoomId), false);
	GameMode->RequestCreateRoom(RoomId);
}

void ASimRTSPlayerController::HandleJoinRoomRequested(const FString& RoomId)
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (GameMode == nullptr || MainMenu == nullptr)
	{
		return;
	}

	MainMenu->SetBusy(true);
	MainMenu->SetStatus(FString::Printf(TEXT("Joining %s..."), *RoomId), false);
	GameMode->RequestJoinRoom(RoomId);
}

void ASimRTSPlayerController::HandleLeaveRequested()
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (GameMode == nullptr || MainMenu == nullptr)
	{
		return;
	}

	MainMenu->SetBusy(true);
	MainMenu->SetStatus(TEXT("Leaving..."), false);
	GameMode->RequestLeaveRoom();
}

void ASimRTSPlayerController::HandleStartClicked()
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (GameMode == nullptr || !GameMode->StartDefaultRoom())
	{
		if (MainMenu != nullptr)
		{
			MainMenu->SetStatus(TEXT("Failed to load level."), true);
		}
		return;
	}

	HideMainMenu();
}

void ASimRTSPlayerController::HandleCommsEvent(const FSimRTSCommsEventView& Event)
{
	ASimRTSGameMode* GameMode = GetWorld() != nullptr ? GetWorld()->GetAuthGameMode<ASimRTSGameMode>() : nullptr;
	if (MainMenu == nullptr || GameMode == nullptr)
	{
		return;
	}

	if (!Event.bOk)
	{
		MainMenu->SetBusy(false);
		const FString Error = Event.Error.IsEmpty() ? TEXT("Request failed.") : Event.Error;
		MainMenu->SetStatus(Error, true);
		return;
	}

	switch (Event.Kind)
	{
	case ESimRTSCommsKind::Login:
		MenuPage = ESimRTSMenuPage::Lobby;
		MainMenu->SetBusy(false);
		MainMenu->SetStatus(TEXT("Fetching rooms..."), false);
		MainMenu->ShowLobby(GameMode->GetMatchmakingNickname(), LastRooms);
		GameMode->RequestGetRooms();
		break;

	case ESimRTSCommsKind::GetRooms:
		LastRooms = Event.Rooms;
		if (MenuPage == ESimRTSMenuPage::Room)
		{
			const FString JoinedId = GameMode->GetJoinedMatchmakingRoomId();
			TArray<FString> PlayerIds;
			for (const FSimRTSCommsRoomView& Room : LastRooms)
			{
				if (Room.Id == JoinedId)
				{
					PlayerIds = Room.PlayerIds;
					break;
				}
			}
			MainMenu->ShowRoom(JoinedId, PlayerIds, GameMode->GetMatchmakingPlayerId());
		}
		else if (MenuPage == ESimRTSMenuPage::Lobby)
		{
			MainMenu->ShowLobby(GameMode->GetMatchmakingNickname(), LastRooms);
			MainMenu->SetStatus(TEXT(""), false);
		}
		break;

	case ESimRTSCommsKind::CreateRoom:
		MainMenu->SetStatus(FString::Printf(TEXT("Joining %s..."), *Event.Room.Id), false);
		break;

	case ESimRTSCommsKind::JoinRoom:
		MenuPage = ESimRTSMenuPage::Room;
		MainMenu->SetBusy(false);
		MainMenu->SetStatus(TEXT(""), false);
		MainMenu->ShowRoom(Event.Room.Id, Event.Room.PlayerIds, GameMode->GetMatchmakingPlayerId());
		break;

	case ESimRTSCommsKind::LeaveRoom:
		MenuPage = ESimRTSMenuPage::Lobby;
		MainMenu->SetBusy(false);
		MainMenu->SetStatus(TEXT("Left room."), false);
		MainMenu->ShowLobby(GameMode->GetMatchmakingNickname(), LastRooms);
		GameMode->RequestGetRooms();
		break;
	}
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
	if (MainMenu != nullptr)
	{
		return;
	}
	BeginGesture(LeftGesture);
}

void ASimRTSPlayerController::OnLeftReleased()
{
	if (MainMenu != nullptr)
	{
		LeftGesture = {};
		return;
	}
	UpdateGestureDrag(LeftGesture);
	if (ResolveClick(LeftGesture) && SelectionManager != nullptr)
	{
		SelectionManager->HandleLeftClick(this);
	}
}

void ASimRTSPlayerController::OnRightPressed()
{
	if (MainMenu != nullptr)
	{
		return;
	}
	BeginGesture(RightGesture);
}

void ASimRTSPlayerController::OnRightReleased()
{
	if (MainMenu != nullptr)
	{
		RightGesture = {};
		return;
	}
	UpdateGestureDrag(RightGesture);
	if (ResolveClick(RightGesture) && OrderManager != nullptr)
	{
		OrderManager->HandleRightClick(this, SelectionManager);
	}
}
