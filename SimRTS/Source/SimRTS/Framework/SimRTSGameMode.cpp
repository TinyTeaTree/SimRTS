#include "SimRTSGameMode.h"

#include "NetworkingLoader.h"
#include "SimRTSDebugHUD.h"
#include "SimRTSObstructionGridVisualizer.h"
#include "SimRTSPathVisualizer.h"
#include "SimRTSPlayerController.h"
#include "SimRTSUnitActor.h"
#include "Types.h"
#include "UnitViewManager.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"

namespace {

FString CommsToFString(const std::string& Value)
{
	return UTF8_TO_TCHAR(Value.c_str());
}

std::string FStringToComms(const FString& Value)
{
	return std::string(TCHAR_TO_UTF8(*Value));
}

FSimRTSCommsRoomView MakeRoomView(const SimRTS::CommsRoom& Room)
{
	FSimRTSCommsRoomView View;
	View.Id = CommsToFString(Room.id);
	View.PlayerIds.Reserve(static_cast<int32>(Room.player_ids.size()));
	for (const std::string& PlayerId : Room.player_ids)
	{
		View.PlayerIds.Add(CommsToFString(PlayerId));
	}
	return View;
}

int32 SimPlayerIdFromLogin(const std::string& Id)
{
	uint32 Hash = 2166136261u;
	for (unsigned char Character : Id)
	{
		Hash ^= Character;
		Hash *= 16777619u;
	}
	const int32 Value = static_cast<int32>(Hash);
	return Value == 0 ? 1 : Value;
}

void RelayMoveOrder(
	SimRTS::CommsClient& Comms,
	const TArray<int32>& UnitIds,
	int32 TargetX,
	int32 TargetY,
	bool bIsNext)
{
	SimRTS::CommsOrder Order;
	Order.sim_player_id = SimPlayerIdFromLogin(Comms.PlayerId());
	Order.target_x = TargetX;
	Order.target_y = TargetY;
	Order.type = 0;
	Order.is_next = bIsNext;
	Order.unit_ids.reserve(UnitIds.Num());
	for (int32 Id : UnitIds)
	{
		Order.unit_ids.push_back(Id);
	}
	Comms.SendOrder(std::move(Order));
}

} // namespace

ASimRTSGameMode::ASimRTSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	DefaultPawnClass = ADefaultPawn::StaticClass();
	PlayerControllerClass = ASimRTSPlayerController::StaticClass();
	HUDClass = ASimRTSDebugHUD::StaticClass();

	Room = CreateDefaultSubobject<USimRTSRoom>(TEXT("Room"));

	SoldierActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSSoldierActor.MySimRTSSoldierActor_C")));
	VehicleActorClass = TSoftClassPtr<ASimRTSUnitActor>(
		FSoftObjectPath(TEXT("/Game/Units/MySimRTSVehicleActor.MySimRTSVehicleActor_C")));
}

void ASimRTSGameMode::BeginPlay()
{
	Super::BeginPlay();

	const FNetworkingLoadResult Networking = NetworkingLoader::LoadDefault();
	if (!Networking.bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("SimRTS GameMode: %s; comms will not start."), *Networking.Error);
	}
	else
	{
		MockTickLag = Networking.Config.MockTickLag;
		Comms.SetHost(FStringToComms(Networking.Config.Ip), Networking.Config.Port, Networking.Config.UdpPort);
		Comms.Start();
		UE_LOG(LogTemp, Log, TEXT("SimRTS comms %s:%d udp=%d mock_tick_lag=%d"),
			*Networking.Config.Ip,
			Networking.Config.Port,
			Networking.Config.UdpPort,
			MockTickLag);
	}

	if (UUnitViewManager* ViewManager = GetUnitViewManager())
	{
		if (UClass* SoldierClass = SoldierActorClass.LoadSynchronous())
		{
			ViewManager->SetActorClassForType(SimRTS::UnitType::Soldier, SoldierClass);
		}
		if (UClass* VehicleClass = VehicleActorClass.LoadSynchronous())
		{
			ViewManager->SetActorClassForType(SimRTS::UnitType::Vehicle, VehicleClass);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SimRTS GameMode ready. Waiting to load room."));
}

void ASimRTSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bMatchmakingMenuOpen = false;
	Comms.Stop();

	if (Room != nullptr)
	{
		Room->Stop(*this);
	}
	DelayedMoveOrders.Reset();
	DestroyDebugVisualizers();

	Super::EndPlay(EndPlayReason);
}

void ASimRTSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PumpComms();
	MaybePollRooms(DeltaSeconds);
}

bool ASimRTSGameMode::StartDefaultRoom()
{
	if (JoinedMatchmakingRoomId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("SimRTS GameMode: join a relay room before starting."));
		return false;
	}

	if (Room == nullptr)
	{
		return false;
	}

	if (Room->IsLoaded())
	{
		return true;
	}

	if (!Room->LoadDefault(*this))
	{
		UE_LOG(LogTemp, Error, TEXT("SimRTS GameMode: room failed to load; sim will not start."));
		return false;
	}

	SetMatchmakingMenuOpen(false);
	SpawnDebugVisualizers();
	return true;
}

void ASimRTSGameMode::RequestLogin(const FString& Username)
{
	bUserRequestPending = true;
	Comms.Login(FStringToComms(Username));
}

void ASimRTSGameMode::RequestGetRooms()
{
	Comms.GetRooms();
}

void ASimRTSGameMode::RequestCreateRoom(const FString& RoomId)
{
	bUserRequestPending = true;
	Comms.CreateRoom(FStringToComms(RoomId));
}

void ASimRTSGameMode::RequestJoinRoom(const FString& RoomId)
{
	bUserRequestPending = true;
	Comms.JoinRoom(FStringToComms(RoomId));
}

void ASimRTSGameMode::RequestLeaveRoom()
{
	if (JoinedMatchmakingRoomId.IsEmpty())
	{
		return;
	}

	bUserRequestPending = true;
	Comms.LeaveRoom(FStringToComms(JoinedMatchmakingRoomId));
}

void ASimRTSGameMode::SetMatchmakingMenuOpen(bool bOpen)
{
	bMatchmakingMenuOpen = bOpen;
	RoomListPollAccum = 0.f;
}

void ASimRTSGameMode::SubmitMoveOrder(const TArray<int32>& UnitIds, int32 TargetX, int32 TargetY, bool bIsNext)
{
	if (!IsRoomLoaded() || JoinedMatchmakingRoomId.IsEmpty() || UnitIds.Num() == 0)
	{
		return;
	}

	if (MockTickLag <= 0)
	{
		RelayMoveOrder(Comms, UnitIds, TargetX, TargetY, bIsNext);
		return;
	}

	FDelayedMoveOrder Delayed;
	Delayed.UnitIds = UnitIds;
	Delayed.TargetX = TargetX;
	Delayed.TargetY = TargetY;
	Delayed.bIsNext = bIsNext;
	Delayed.SendAtTick = GetBridge().GetTick() + MockTickLag;
	DelayedMoveOrders.Add(MoveTemp(Delayed));
}

void ASimRTSGameMode::FlushDelayedMoveOrders()
{
	if (!IsRoomLoaded() || DelayedMoveOrders.Num() == 0)
	{
		return;
	}

	const int32 CurrentTick = GetBridge().GetTick();
	for (int32 Index = DelayedMoveOrders.Num() - 1; Index >= 0; --Index)
	{
		const FDelayedMoveOrder& Delayed = DelayedMoveOrders[Index];
		if (CurrentTick < Delayed.SendAtTick)
		{
			continue;
		}

		RelayMoveOrder(Comms, Delayed.UnitIds, Delayed.TargetX, Delayed.TargetY, Delayed.bIsNext);
		DelayedMoveOrders.RemoveAt(Index);
	}
}

bool ASimRTSGameMode::IsMatchmakingLoggedIn() const
{
	return !Comms.SessionToken().empty();
}

FString ASimRTSGameMode::GetMatchmakingNickname() const
{
	return CommsToFString(Comms.Nickname());
}

FString ASimRTSGameMode::GetMatchmakingPlayerId() const
{
	return CommsToFString(Comms.PlayerId());
}

void ASimRTSGameMode::PumpComms()
{
	SimRTS::CommsEvent Event;
	while (Comms.TryPop(Event))
	{
		if (Event.kind == SimRTS::CommsEventKind::Order)
		{
			if (Event.result.ok && IsRoomLoaded())
			{
				TArray<int32> UnitIds;
				UnitIds.Reserve(static_cast<int32>(Event.result.order.unit_ids.size()));
				for (int32_t Id : Event.result.order.unit_ids)
				{
					UnitIds.Add(Id);
				}
				GetBridge().SubmitMoveOrder(
					UnitIds,
					Event.result.order.target_x,
					Event.result.order.target_y,
					Event.result.order.is_next,
					Event.result.order.sim_player_id);
			}
			continue;
		}

		const FSimRTSCommsEventView View = MakeCommsView(Event);

		if (Event.kind == SimRTS::CommsEventKind::CreateRoom && Event.result.ok)
		{
			Comms.JoinRoom(Event.result.room.id);
			OnCommsEvent.Broadcast(View);
			continue;
		}

		if (Event.kind == SimRTS::CommsEventKind::JoinRoom && Event.result.ok)
		{
			JoinedMatchmakingRoomId = View.Room.Id;
			if (JoinedMatchmakingRoomId.IsEmpty())
			{
				JoinedMatchmakingRoomId = CommsToFString(Event.result.room.id);
			}
			Comms.GetRooms();
		}
		else if (Event.kind == SimRTS::CommsEventKind::LeaveRoom && Event.result.ok)
		{
			JoinedMatchmakingRoomId.Empty();
			DelayedMoveOrders.Reset();
		}

		if (Event.kind != SimRTS::CommsEventKind::GetRooms)
		{
			bUserRequestPending = false;
		}

		OnCommsEvent.Broadcast(View);
	}
}

void ASimRTSGameMode::MaybePollRooms(float DeltaSeconds)
{
	if (!bMatchmakingMenuOpen || !IsMatchmakingLoggedIn() || bUserRequestPending || IsRoomLoaded())
	{
		return;
	}

	RoomListPollAccum += DeltaSeconds;
	if (RoomListPollAccum < RoomListPollSeconds)
	{
		return;
	}

	RoomListPollAccum = 0.f;
	Comms.GetRooms();
}

FSimRTSCommsEventView ASimRTSGameMode::MakeCommsView(const SimRTS::CommsEvent& Event) const
{
	FSimRTSCommsEventView View;
	switch (Event.kind)
	{
	case SimRTS::CommsEventKind::Login:
		View.Kind = ESimRTSCommsKind::Login;
		break;
	case SimRTS::CommsEventKind::GetRooms:
		View.Kind = ESimRTSCommsKind::GetRooms;
		break;
	case SimRTS::CommsEventKind::CreateRoom:
		View.Kind = ESimRTSCommsKind::CreateRoom;
		break;
	case SimRTS::CommsEventKind::JoinRoom:
		View.Kind = ESimRTSCommsKind::JoinRoom;
		break;
	case SimRTS::CommsEventKind::LeaveRoom:
		View.Kind = ESimRTSCommsKind::LeaveRoom;
		break;
	case SimRTS::CommsEventKind::Order:
		View.Kind = ESimRTSCommsKind::JoinRoom;
		break;
	}

	View.bOk = Event.result.ok;
	View.HttpStatus = Event.result.http_status;
	View.Error = CommsToFString(Event.result.error);
	View.Nickname = CommsToFString(Event.result.session.nickname);
	View.PlayerId = CommsToFString(Event.result.session.player_id);
	if (View.Nickname.IsEmpty())
	{
		View.Nickname = GetMatchmakingNickname();
	}
	if (View.PlayerId.IsEmpty())
	{
		View.PlayerId = GetMatchmakingPlayerId();
	}
	View.Room = MakeRoomView(Event.result.room);
	View.Rooms.Reserve(static_cast<int32>(Event.result.rooms.size()));
	for (const SimRTS::CommsRoom& RoomData : Event.result.rooms)
	{
		View.Rooms.Add(MakeRoomView(RoomData));
	}
	return View;
}

void ASimRTSGameMode::SpawnDebugVisualizers()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Room == nullptr)
	{
		return;
	}

	if (bShowObstructionGrid)
	{
		ObstructionGridVisualizer = World->SpawnActor<ASimRTSObstructionGridVisualizer>();
		if (ObstructionGridVisualizer)
		{
			ObstructionGridVisualizer->Build(Room->GetBridge().GetStaticData().pathing, GridScale, ObstructionGridMaxBlueDistance);
		}
	}

	if (bShowUnitPaths)
	{
		PathVisualizer = World->SpawnActor<ASimRTSPathVisualizer>();
	}
}

void ASimRTSGameMode::DestroyDebugVisualizers()
{
	if (ObstructionGridVisualizer)
	{
		ObstructionGridVisualizer->Destroy();
		ObstructionGridVisualizer = nullptr;
	}

	if (PathVisualizer)
	{
		PathVisualizer->Destroy();
		PathVisualizer = nullptr;
	}
}

FVector ASimRTSGameMode::GridToWorld(int32 X, int32 Y) const
{
	const float WorldW = static_cast<float>(GetBridge().GetStaticData().world_width) * GridScale;
	const float WorldH = static_cast<float>(GetBridge().GetStaticData().world_height) * GridScale;
	const float XWorld = static_cast<float>(X) * GridScale - WorldW * 0.5f;
	const float YWorld = static_cast<float>(Y) * GridScale - WorldH * 0.5f;
	return FVector(XWorld, YWorld, 0.f);
}

bool ASimRTSGameMode::WorldToGrid(const FVector& WorldLocation, int32& OutX, int32& OutY) const
{
	if (GridScale <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const int32 WorldWidth = GetBridge().GetStaticData().world_width;
	const int32 WorldHeight = GetBridge().GetStaticData().world_height;
	const float WorldW = static_cast<float>(WorldWidth) * GridScale;
	const float WorldH = static_cast<float>(WorldHeight) * GridScale;

	const int32 GridX = FMath::FloorToInt((WorldLocation.X + WorldW * 0.5f) / GridScale);
	const int32 GridY = FMath::FloorToInt((WorldLocation.Y + WorldH * 0.5f) / GridScale);

	OutX = FMath::Clamp(GridX, 0, FMath::Max(0, WorldWidth - 1));
	OutY = FMath::Clamp(GridY, 0, FMath::Max(0, WorldHeight - 1));
	return true;
}
