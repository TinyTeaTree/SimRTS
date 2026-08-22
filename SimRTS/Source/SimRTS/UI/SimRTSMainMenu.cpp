#include "SimRTSMainMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace {

const FLinearColor kButtonGreen(0.16f, 0.52f, 0.32f);
const FLinearColor kButtonBlue(0.16f, 0.32f, 0.55f);
const FLinearColor kButtonGray(0.28f, 0.28f, 0.32f);
const FLinearColor kPanelBg(0.05f, 0.06f, 0.09f, 0.96f);

UTextBlock* MakeText(UWidgetTree* Tree, const FName Name, const FString& Text, int32 Size, bool bBold)
{
	UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Label->SetText(FText::FromString(Text));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
	return Label;
}

void CenterButtonLabel(UWidget* Label)
{
	if (UButtonSlot* Slot = Cast<UButtonSlot>(Label->Slot))
	{
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
	}
}

UButton* MakeButton(UWidgetTree* Tree, const FName Name, const FString& Label, const FLinearColor& Color)
{
	UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetBackgroundColor(Color);
	UTextBlock* Text = MakeText(Tree, *FString::Printf(TEXT("%sLabel"), *Name.ToString()), Label, 16, true);
	Text->SetJustification(ETextJustify::Center);
	Button->AddChild(Text);
	CenterButtonLabel(Text);
	return Button;
}

UEditableTextBox* MakeTextBox(UWidgetTree* Tree, const FName Name, const FString& Hint)
{
	UEditableTextBox* Box = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
	Box->SetHintText(FText::FromString(Hint));

	const FSlateColor TypedText(FLinearColor(0.06f, 0.06f, 0.07f, 1.f));
	Box->WidgetStyle.SetForegroundColor(TypedText);
	Box->WidgetStyle.SetFocusedForegroundColor(TypedText);
	Box->WidgetStyle.SetReadOnlyForegroundColor(TypedText);
	Box->WidgetStyle.TextStyle.SetColorAndOpacity(TypedText);
	Box->WidgetStyle.SetBackgroundColor(FLinearColor(0.92f, 0.93f, 0.95f, 1.f));
	Box->SetForegroundColor(FLinearColor(0.06f, 0.06f, 0.07f, 1.f));
	return Box;
}

void AddV(UVerticalBox* Box, UWidget* Child, float BottomPad, ESlateSizeRule::Type SizeRule = ESlateSizeRule::Automatic, float Fill = 1.f)
{
	UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Child);
	Slot->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPad));
	Slot->SetHorizontalAlignment(HAlign_Fill);
	FSlateChildSize Size(SizeRule);
	Size.Value = Fill;
	Slot->SetSize(Size);
}

} // namespace

void USimRTSMenuCallback::HandleJoin()
{
	if (Menu != nullptr)
	{
		Menu->RequestJoin(RoomId);
	}
}

USimRTSMainMenu::USimRTSMainMenu(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> USimRTSMainMenu::RebuildWidget()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		BuildTreeIfNeeded();
	}
	return Super::RebuildWidget();
}

void USimRTSMainMenu::BuildTreeIfNeeded()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UBorder* Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
	Dimmer->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f));
	Dimmer->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* DimmerSlot = Root->AddChildToCanvas(Dimmer))
	{
		DimmerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimmerSlot->SetOffsets(FMargin(0.f));
	}

	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSize"));
	PanelSize->SetWidthOverride(560.f);
	PanelSize->SetHeightOverride(520.f);
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(PanelSize))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetSize(FVector2D(560.f, 520.f));
		PanelSlot->SetPosition(FVector2D::ZeroVector);
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(kPanelBg);
	Panel->SetPadding(FMargin(24.f));
	PanelSize->AddChild(Panel);

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Stack"));
	Panel->SetContent(Stack);

	AddV(Stack, MakeText(WidgetTree, TEXT("Title"), TEXT("SimRTS"), 28, true), 8.f);
	StatusText = MakeText(WidgetTree, TEXT("Status"), TEXT(""), 14, false);
	AddV(Stack, StatusText, 16.f);

	LoginPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LoginPage"));
	UsernameBox = MakeTextBox(WidgetTree, TEXT("UsernameBox"), TEXT("Username"));
	UsernameBox->OnTextCommitted.AddDynamic(this, &USimRTSMainMenu::HandleUsernameCommitted);
	AddV(LoginPage, UsernameBox, 12.f);
	LoginButton = MakeButton(WidgetTree, TEXT("LoginButton"), TEXT("Login"), kButtonGreen);
	LoginButton->OnClicked.AddDynamic(this, &USimRTSMainMenu::HandleLoginClicked);
	AddV(LoginPage, LoginButton, 0.f);
	AddV(Stack, LoginPage, 0.f);

	LobbyPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LobbyPage"));
	LobbyHeader = MakeText(WidgetTree, TEXT("LobbyHeader"), TEXT(""), 16, false);
	AddV(LobbyPage, LobbyHeader, 8.f);
	USizeBox* RoomListSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RoomListSize"));
	RoomListSize->SetHeightOverride(240.f);
	RoomList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RoomList"));
	RoomListSize->AddChild(RoomList);
	AddV(LobbyPage, RoomListSize, 12.f);
	UHorizontalBox* CreateRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CreateRow"));
	CreateRoomNameBox = MakeTextBox(WidgetTree, TEXT("CreateRoomNameBox"), TEXT("Room name"));
	CreateRoomNameBox->OnTextCommitted.AddDynamic(this, &USimRTSMainMenu::HandleRoomNameCommitted);
	if (UHorizontalBoxSlot* NameSlot = CreateRow->AddChildToHorizontalBox(CreateRoomNameBox))
	{
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
	}
	CreateRoomButton = MakeButton(WidgetTree, TEXT("CreateRoomButton"), TEXT("Create"), kButtonBlue);
	CreateRoomButton->OnClicked.AddDynamic(this, &USimRTSMainMenu::HandleCreateClicked);
	if (UHorizontalBoxSlot* CreateSlot = CreateRow->AddChildToHorizontalBox(CreateRoomButton))
	{
		CreateSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		CreateSlot->SetVerticalAlignment(VAlign_Fill);
	}
	AddV(LobbyPage, CreateRow, 0.f);
	LobbyPage->SetVisibility(ESlateVisibility::Collapsed);
	AddV(Stack, LobbyPage, 0.f, ESlateSizeRule::Fill);

	RoomPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoomPage"));
	RoomHeader = MakeText(WidgetTree, TEXT("RoomHeader"), TEXT(""), 18, true);
	AddV(RoomPage, RoomHeader, 8.f);
	AddV(RoomPage, MakeText(WidgetTree, TEXT("PlayersLabel"), TEXT("Players"), 14, false), 6.f);
	USizeBox* PlayerListSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayerListSize"));
	PlayerListSize->SetHeightOverride(180.f);
	PlayerList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PlayerList"));
	PlayerListSize->AddChild(PlayerList);
	AddV(RoomPage, PlayerListSize, 12.f);
	UHorizontalBox* RoomButtons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoomButtons"));
	LeaveButton = MakeButton(WidgetTree, TEXT("LeaveButton"), TEXT("Leave"), kButtonGray);
	LeaveButton->OnClicked.AddDynamic(this, &USimRTSMainMenu::HandleLeaveClicked);
	if (UHorizontalBoxSlot* LeaveSlot = RoomButtons->AddChildToHorizontalBox(LeaveButton))
	{
		LeaveSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LeaveSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	}
	StartButton = MakeButton(WidgetTree, TEXT("StartButton"), TEXT("Start"), kButtonGreen);
	StartButton->OnClicked.AddDynamic(this, &USimRTSMainMenu::HandleStartClicked);
	if (UHorizontalBoxSlot* StartSlot = RoomButtons->AddChildToHorizontalBox(StartButton))
	{
		StartSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	AddV(RoomPage, RoomButtons, 0.f);
	RoomPage->SetVisibility(ESlateVisibility::Collapsed);
	AddV(Stack, RoomPage, 0.f, ESlateSizeRule::Fill);
}

void USimRTSMainMenu::SetPageVisible(UVerticalBox* Page)
{
	if (LoginPage == nullptr || LobbyPage == nullptr || RoomPage == nullptr)
	{
		return;
	}

	LoginPage->SetVisibility(Page == LoginPage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LobbyPage->SetVisibility(Page == LobbyPage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	RoomPage->SetVisibility(Page == RoomPage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void USimRTSMainMenu::ShowLogin()
{
	SetPageVisible(LoginPage);
}

void USimRTSMainMenu::ShowLobby(const FString& Nickname, const TArray<FSimRTSCommsRoomView>& Rooms)
{
	if (LobbyHeader == nullptr || LobbyPage == nullptr)
	{
		return;
	}

	LobbyHeader->SetText(FText::FromString(FString::Printf(TEXT("Logged in as %s"), *Nickname)));
	RebuildRoomList(Rooms);
	SetPageVisible(LobbyPage);
}

void USimRTSMainMenu::ShowRoom(const FString& RoomId, const TArray<FString>& PlayerIds, const FString& LocalPlayerId)
{
	if (RoomHeader == nullptr || RoomPage == nullptr)
	{
		return;
	}

	RoomHeader->SetText(FText::FromString(FString::Printf(TEXT("Room: %s"), *RoomId)));
	RebuildPlayerList(PlayerIds, LocalPlayerId);
	SetPageVisible(RoomPage);
}

void USimRTSMainMenu::SetStatus(const FString& Message, bool bError)
{
	if (StatusText == nullptr)
	{
		return;
	}

	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(0.8f, 0.85f, 0.9f)));
}

void USimRTSMainMenu::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
	const bool bEnabled = !bBusy;
	if (LoginButton) LoginButton->SetIsEnabled(bEnabled);
	if (CreateRoomButton) CreateRoomButton->SetIsEnabled(bEnabled);
	if (LeaveButton) LeaveButton->SetIsEnabled(bEnabled);
	if (StartButton) StartButton->SetIsEnabled(bEnabled);
	if (UsernameBox) UsernameBox->SetIsReadOnly(bBusy);
	if (CreateRoomNameBox) CreateRoomNameBox->SetIsReadOnly(bBusy);
	for (UButton* JoinButton : RoomJoinButtons)
	{
		if (JoinButton)
		{
			JoinButton->SetIsEnabled(bEnabled);
		}
	}
}

void USimRTSMainMenu::RequestJoin(const FString& RoomId)
{
	if (bBusy)
	{
		return;
	}
	OnJoinRoomRequested.Broadcast(RoomId);
}

void USimRTSMainMenu::RebuildRoomList(const TArray<FSimRTSCommsRoomView>& Rooms)
{
	if (RoomList == nullptr || WidgetTree == nullptr)
	{
		return;
	}
	RoomList->ClearChildren();
	RoomJoinCallbacks.Reset();
	RoomJoinButtons.Reset();

	if (Rooms.Num() == 0)
	{
		RoomList->AddChild(MakeText(WidgetTree, TEXT("EmptyRooms"), TEXT("No rooms yet."), 14, false));
		return;
	}

	for (int32 Index = 0; Index < Rooms.Num(); ++Index)
	{
		const FSimRTSCommsRoomView& Room = Rooms[Index];
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			*FString::Printf(TEXT("RoomRow_%d"), Index));

		UTextBlock* Name = MakeText(
			WidgetTree,
			*FString::Printf(TEXT("RoomName_%d"), Index),
			Room.Id,
			16,
			true);
		if (UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(Name))
		{
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* Count = MakeText(
			WidgetTree,
			*FString::Printf(TEXT("RoomCount_%d"), Index),
			FString::Printf(TEXT("%d"), Room.PlayerIds.Num()),
			16,
			false);
		if (UHorizontalBoxSlot* CountSlot = Row->AddChildToHorizontalBox(Count))
		{
			CountSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			CountSlot->SetPadding(FMargin(8.f, 0.f));
			CountSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* JoinButton = MakeButton(
			WidgetTree,
			*FString::Printf(TEXT("Join_%d"), Index),
			TEXT("Join"),
			kButtonGreen);
		JoinButton->SetIsEnabled(!bBusy);
		USimRTSMenuCallback* Callback = NewObject<USimRTSMenuCallback>(this);
		Callback->RoomId = Room.Id;
		Callback->Menu = this;
		JoinButton->OnClicked.AddDynamic(Callback, &USimRTSMenuCallback::HandleJoin);
		RoomJoinCallbacks.Add(Callback);
		RoomJoinButtons.Add(JoinButton);
		if (UHorizontalBoxSlot* JoinSlot = Row->AddChildToHorizontalBox(JoinButton))
		{
			JoinSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		RoomList->AddChild(Row);
	}
}

void USimRTSMainMenu::RebuildPlayerList(const TArray<FString>& PlayerIds, const FString& LocalPlayerId)
{
	if (PlayerList == nullptr || WidgetTree == nullptr)
	{
		return;
	}
	PlayerList->ClearChildren();
	if (PlayerIds.Num() == 0)
	{
		PlayerList->AddChild(MakeText(WidgetTree, TEXT("EmptyPlayers"), TEXT("No players."), 14, false));
		return;
	}

	for (int32 Index = 0; Index < PlayerIds.Num(); ++Index)
	{
		FString Line = PlayerIds[Index];
		if (Line == LocalPlayerId)
		{
			Line += TEXT(" (you)");
		}
		PlayerList->AddChild(MakeText(
			WidgetTree,
			*FString::Printf(TEXT("Player_%d"), Index),
			Line,
			16,
			false));
	}
}

void USimRTSMainMenu::HandleLoginClicked()
{
	OnLoginRequested.Broadcast(UsernameBox->GetText().ToString().TrimStartAndEnd());
}

void USimRTSMainMenu::HandleCreateClicked()
{
	OnCreateRoomRequested.Broadcast(CreateRoomNameBox->GetText().ToString().TrimStartAndEnd());
}

void USimRTSMainMenu::HandleLeaveClicked()
{
	OnLeaveRequested.Broadcast();
}

void USimRTSMainMenu::HandleStartClicked()
{
	OnStartClicked.Broadcast();
}

void USimRTSMainMenu::HandleUsernameCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		OnLoginRequested.Broadcast(Text.ToString().TrimStartAndEnd());
	}
}

void USimRTSMainMenu::HandleRoomNameCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		OnCreateRoomRequested.Broadcast(Text.ToString().TrimStartAndEnd());
	}
}
