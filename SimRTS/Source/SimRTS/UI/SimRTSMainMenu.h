#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SimRTSCommsView.h"
#include "SimRTSMainMenu.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class USimRTSMainMenu;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimRTSMenuLogin, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimRTSMenuCreateRoom, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimRTSMenuJoinRoom, const FString&);
DECLARE_MULTICAST_DELEGATE(FOnSimRTSMenuLeave);
DECLARE_MULTICAST_DELEGATE(FOnSimRTSMenuStart);

UCLASS()
class USimRTSMenuCallback : public UObject
{
	GENERATED_BODY()

public:
	FString RoomId;

	UPROPERTY()
	TObjectPtr<USimRTSMainMenu> Menu;

	UFUNCTION()
	void HandleJoin();
};

UCLASS()
class SIMRTS_API USimRTSMainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	USimRTSMainMenu(const FObjectInitializer& ObjectInitializer);

	FOnSimRTSMenuLogin OnLoginRequested;
	FOnSimRTSMenuCreateRoom OnCreateRoomRequested;
	FOnSimRTSMenuJoinRoom OnJoinRoomRequested;
	FOnSimRTSMenuLeave OnLeaveRequested;
	FOnSimRTSMenuStart OnStartClicked;

	void ShowLogin();
	void ShowLobby(const FString& Nickname, const TArray<FSimRTSCommsRoomView>& Rooms);
	void ShowRoom(const FString& RoomId, const TArray<FString>& PlayerIds, const FString& LocalPlayerId);
	void SetStatus(const FString& Message, bool bError);
	void SetBusy(bool bBusy);

	void RequestJoin(const FString& RoomId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void BuildTreeIfNeeded();
	void SetPageVisible(UVerticalBox* Page);
	void RebuildRoomList(const TArray<FSimRTSCommsRoomView>& Rooms);
	void RebuildPlayerList(const TArray<FString>& PlayerIds, const FString& LocalPlayerId);

	UFUNCTION()
	void HandleLoginClicked();
	UFUNCTION()
	void HandleCreateClicked();
	UFUNCTION()
	void HandleLeaveClicked();
	UFUNCTION()
	void HandleStartClicked();
	UFUNCTION()
	void HandleUsernameCommitted(const FText& Text, ETextCommit::Type CommitType);
	UFUNCTION()
	void HandleRoomNameCommitted(const FText& Text, ETextCommit::Type CommitType);

	UPROPERTY()
	TObjectPtr<UVerticalBox> LoginPage;

	UPROPERTY()
	TObjectPtr<UVerticalBox> LobbyPage;

	UPROPERTY()
	TObjectPtr<UVerticalBox> RoomPage;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> UsernameBox;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> CreateRoomNameBox;

	UPROPERTY()
	TObjectPtr<UButton> LoginButton;

	UPROPERTY()
	TObjectPtr<UButton> CreateRoomButton;

	UPROPERTY()
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY()
	TObjectPtr<UButton> StartButton;

	UPROPERTY()
	TObjectPtr<UScrollBox> RoomList;

	UPROPERTY()
	TObjectPtr<UScrollBox> PlayerList;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> LobbyHeader;

	UPROPERTY()
	TObjectPtr<UTextBlock> RoomHeader;

	UPROPERTY()
	TArray<TObjectPtr<USimRTSMenuCallback>> RoomJoinCallbacks;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> RoomJoinButtons;

	bool bBusy = false;
};
