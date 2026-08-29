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
#include "TimerManager.h"

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
	bool bIsNext,
	uint32 OrderId,
	int32 ActualTick,
	int32 HashTick,
	uint64 StateHash)
{
	SimRTS::CommsOrder Order;
	Order.sim_player_id = SimPlayerIdFromLogin(Comms.PlayerId());
	Order.order_id = OrderId;
	Order.actual_tick = ActualTick;
	Order.hash_tick = HashTick;
	Order.state_hash = StateHash;
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
		FutureTickDistance = Networking.Config.FutureTickDistance;
		MinTickDelaySeconds = Networking.Config.PacerMinDelayMs / 1000.f;
		Comms.SetHost(FStringToComms(Networking.Config.Ip), Networking.Config.Port, Networking.Config.UdpPort);
		Comms.SetPingConfig(Networking.Config.PingIntervalMs, Networking.Config.PingKeepAmount);
		Comms.Start();
		UE_LOG(LogTemp, Log, TEXT("SimRTS comms %s:%d udp=%d future_tick_distance=%d ping=%dms keep=%d pacer_min_delay=%dms"),
			*Networking.Config.Ip,
			Networking.Config.Port,
			Networking.Config.UdpPort,
			FutureTickDistance,
			Networking.Config.PingIntervalMs,
			Networking.Config.PingKeepAmount,
			Networking.Config.PacerMinDelayMs);
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(KickoffWaitHandle);
	}
	Comms.Stop();

	if (Room != nullptr)
	{
		Room->Stop(*this);
	}
	ResetHashHistory();
	DestroyDebugVisualizers();

	Super::EndPlay(EndPlayReason);
}

void ASimRTSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PumpComms();
	MaybePollRooms(DeltaSeconds);
	MaybeSendEmptyOrders();
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
	if (PendingKickoffRemainingMs >= 0)
	{
		const uint32 Id = PendingKickoffId;
		const int32 Remaining = PendingKickoffRemainingMs;
		PendingKickoffRemainingMs = -1;
		HandleKickoff(Id, Remaining);
	}
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

void ASimRTSGameMode::RequestStartRoom()
{
	if (JoinedMatchmakingRoomId.IsEmpty())
	{
		return;
	}

	bUserRequestPending = true;
	Comms.StartRoom(FStringToComms(JoinedMatchmakingRoomId));
}

void ASimRTSGameMode::SetMatchmakingMenuOpen(bool bOpen)
{
	bMatchmakingMenuOpen = bOpen;
	RoomListPollAccum = 0.f;
}

void ASimRTSGameMode::SubmitMoveOrder(const TArray<int32>& UnitIds, int32 TargetX, int32 TargetY, bool bIsNext)
{
	if (!IsRoomLoaded() || !IsSimClockStarted() || JoinedMatchmakingRoomId.IsEmpty() || UnitIds.Num() == 0)
	{
		return;
	}

	const int32 ActualTick = GetActualTick();
	if (ActualTick > LastCoveredActualTick)
	{
		ActualTicksWithClicks.Add(ActualTick);
	}

	int32 HashTick = 0;
	uint64 StateHash = 0;
	LatestOrderHash(HashTick, StateHash);
	RelayMoveOrder(Comms, UnitIds, TargetX, TargetY, bIsNext, NextOrderId++, ActualTick, HashTick, StateHash);
	NoteCommandFrame(SimPlayerIdFromLogin(Comms.PlayerId()), ActualTick);
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

int32 ASimRTSGameMode::GetMinRttMs() const
{
	return Comms.MinRttMs();
}

void ASimRTSGameMode::SetSimTickHalted(bool bHalted)
{
	if (Room != nullptr)
	{
		Room->SetTickHalted(bHalted);
	}
}

bool ASimRTSGameMode::IsSimTickHalted() const
{
	return Room != nullptr && Room->IsTickHalted();
}

int32 ASimRTSGameMode::GetSimTicksBehind() const
{
	return Room != nullptr ? Room->GetTicksBehind() : 0;
}

int32 ASimRTSGameMode::GetActualTick() const
{
	return Room != nullptr ? Room->GetActualTick() : 0;
}

bool ASimRTSGameMode::IsDesynced() const
{
	return bDesynced;
}

void ASimRTSGameMode::ResetHashHistory()
{
	LocalHashes.Reset();
	PendingRemoteHashes.Reset();
	bDesynced = false;
	LastCoveredActualTick = -1;
	ActualTicksWithClicks.Reset();
	SeatedSimPlayerIds.Reset();
	CommandFramesByPlayer.Reset();
}

void ASimRTSGameMode::SnapshotSeatedPlayers(const std::vector<std::string>& PlayerIds)
{
	SeatedSimPlayerIds.Reset();
	SeatedSimPlayerIds.Reserve(static_cast<int32>(PlayerIds.size()));
	for (const std::string& PlayerId : PlayerIds)
	{
		SeatedSimPlayerIds.Add(SimPlayerIdFromLogin(PlayerId));
	}
	SeatedSimPlayerIds.Sort();
	UE_LOG(LogTemp, Log, TEXT("SimRTS seated set count=%d"), SeatedSimPlayerIds.Num());
}

void ASimRTSGameMode::NoteCommandFrame(int32 SimPlayerId, int32 ActualTick)
{
	CommandFramesByPlayer.FindOrAdd(SimPlayerId).Add(ActualTick);
}

void ASimRTSGameMode::PruneCommandFrames()
{
	if (!IsRoomLoaded())
	{
		return;
	}

	const int32 Tick = GetBridge().GetTick();
	if (Tick < FutureTickDistance)
	{
		return;
	}

	const int32 KeepFrom = Tick - FutureTickDistance;
	for (TPair<int32, TSet<int32>>& Pair : CommandFramesByPlayer)
	{
		for (TSet<int32>::TIterator It(Pair.Value); It; ++It)
		{
			if (*It < KeepFrom)
			{
				It.RemoveCurrent();
			}
		}
	}
}

bool ASimRTSGameMode::HasAllCommandsForSimTick() const
{
	if (!IsRoomLoaded() || SeatedSimPlayerIds.Num() == 0)
	{
		return true;
	}

	const int32 Tick = GetBridge().GetTick();
	if (Tick < FutureTickDistance)
	{
		return true;
	}

	const int32 NeededActualTick = Tick - FutureTickDistance;
	for (const int32 PlayerId : SeatedSimPlayerIds)
	{
		const TSet<int32>* Frames = CommandFramesByPlayer.Find(PlayerId);
		if (Frames == nullptr || !Frames->Contains(NeededActualTick))
		{
			return false;
		}
	}
	return true;
}

bool ASimRTSGameMode::IsSimLocked() const
{
	return IsSimClockStarted() && !HasAllCommandsForSimTick();
}

void ASimRTSGameMode::LatestOrderHash(int32& OutHashTick, uint64& OutStateHash) const
{
	OutHashTick = 0;
	OutStateHash = 0;
	if (LocalHashes.Num() > 0)
	{
		OutHashTick = LocalHashes.Last().Key;
		OutStateHash = LocalHashes.Last().Value;
	}
}

void ASimRTSGameMode::MaybeSendEmptyOrders()
{
	if (!IsRoomLoaded() || !IsSimClockStarted() || JoinedMatchmakingRoomId.IsEmpty())
	{
		return;
	}

	const int32 Current = GetActualTick();
	if (LastCoveredActualTick < 0)
	{
		LastCoveredActualTick = Current - 1;
	}

	int32 HashTick = 0;
	uint64 StateHash = 0;
	LatestOrderHash(HashTick, StateHash);

	for (int32 At = LastCoveredActualTick + 1; At <= Current; ++At)
	{
		if (ActualTicksWithClicks.Contains(At))
		{
			ActualTicksWithClicks.Remove(At);
			LastCoveredActualTick = At;
			continue;
		}

		RelayMoveOrder(Comms, TArray<int32>(), 0, 0, false, NextOrderId++, At, HashTick, StateHash);
		NoteCommandFrame(SimPlayerIdFromLogin(Comms.PlayerId()), At);
		LastCoveredActualTick = At;
	}
}

bool ASimRTSGameMode::TryGetLocalHash(int32 Tick, uint64& OutHash) const
{
	for (int32 Index = LocalHashes.Num() - 1; Index >= 0; --Index)
	{
		if (LocalHashes[Index].Key == Tick)
		{
			OutHash = LocalHashes[Index].Value;
			return true;
		}
	}
	return false;
}

void ASimRTSGameMode::ComparePeerHash(int32 HashTick, uint64 StateHash)
{
	if (bDesynced)
	{
		return;
	}

	uint64 LocalHash = 0;
	if (TryGetLocalHash(HashTick, LocalHash))
	{
		if (LocalHash != StateHash)
		{
			bDesynced = true;
			UE_LOG(LogTemp, Error, TEXT("SimRTS desync hash_tick=%d local=%llu peer=%llu"),
				HashTick,
				static_cast<unsigned long long>(LocalHash),
				static_cast<unsigned long long>(StateHash));
		}
		return;
	}

	if (LocalHashes.Num() > 0 && HashTick < LocalHashes[0].Key)
	{
		return;
	}

	PendingRemoteHashes.Add({HashTick, StateHash});
}

void ASimRTSGameMode::RecordGameplayHash()
{
	if (!IsRoomLoaded())
	{
		return;
	}

	const int32 Tick = GetBridge().GetTick();
	const uint64 Hash = GetBridge().GetGameplayHash();
	if (LocalHashes.Num() == 0 || LocalHashes.Last().Key != Tick)
	{
		LocalHashes.Add({Tick, Hash});
		while (LocalHashes.Num() > 600)
		{
			LocalHashes.RemoveAt(0);
		}
	}
	else
	{
		LocalHashes.Last().Value = Hash;
	}

	for (int32 Index = PendingRemoteHashes.Num() - 1; Index >= 0; --Index)
	{
		const int32 RemoteTick = PendingRemoteHashes[Index].Key;
		const uint64 RemoteHash = PendingRemoteHashes[Index].Value;
		uint64 LocalHash = 0;
		if (TryGetLocalHash(RemoteTick, LocalHash))
		{
			PendingRemoteHashes.RemoveAt(Index);
			if (LocalHash != RemoteHash)
			{
				bDesynced = true;
				UE_LOG(LogTemp, Error, TEXT("SimRTS desync hash_tick=%d local=%llu peer=%llu"),
					RemoteTick,
					static_cast<unsigned long long>(LocalHash),
					static_cast<unsigned long long>(RemoteHash));
			}
		}
		else if (LocalHashes.Num() > 0 && RemoteTick < LocalHashes[0].Key)
		{
			PendingRemoteHashes.RemoveAt(Index);
		}
	}

	PruneCommandFrames();
}

void ASimRTSGameMode::HandleKickoff(uint32 KickoffId, int32 RemainingMs)
{
	if (bKickoffArmed)
	{
		return;
	}

	if (!IsRoomLoaded())
	{
		PendingKickoffId = KickoffId;
		PendingKickoffRemainingMs = RemainingMs;
		return;
	}

	bKickoffArmed = true;
	ArmedKickoffId = KickoffId;
	int32 WaitMs = RemainingMs;
	const int32 RttMs = Comms.MinRttMs();
	if (RttMs >= 0)
	{
		WaitMs = FMath::Max(0, RemainingMs - RttMs / 2);
	}

	UE_LOG(LogTemp, Log, TEXT("SimRTS kickoff id=%u remaining=%dms rtt=%dms wait=%dms"),
		KickoffId,
		RemainingMs,
		RttMs,
		WaitMs);

	if (WaitMs <= 0)
	{
		ArmSimClock();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			KickoffWaitHandle,
			this,
			&ASimRTSGameMode::OnKickoffWaitElapsed,
			static_cast<float>(WaitMs) / 1000.f,
			false);
	}
}

void ASimRTSGameMode::OnKickoffWaitElapsed()
{
	ArmSimClock();
}

void ASimRTSGameMode::ArmSimClock()
{
	if (Room != nullptr)
	{
		Room->StartClock(*this);
	}
}

void ASimRTSGameMode::PumpComms()
{
	SimRTS::CommsEvent Event;
	while (Comms.TryPop(Event))
	{
		if (Event.kind == SimRTS::CommsEventKind::Kickoff)
		{
			if (Event.result.ok)
			{
				HandleKickoff(Event.result.kickoff.kickoff_id, Event.result.kickoff.remaining_ms);
			}
			continue;
		}

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
				NoteCommandFrame(Event.result.order.sim_player_id, Event.result.order.actual_tick);
				if (UnitIds.Num() > 0)
				{
					const int32 ScheduledTick = Event.result.order.actual_tick + FutureTickDistance;
					if (ScheduledTick < GetBridge().GetTick())
					{
						UE_LOG(LogTemp, Warning, TEXT("SimRTS late order id=%u actual=%d scheduled=%d sim=%d"),
							Event.result.order.order_id,
							Event.result.order.actual_tick,
							ScheduledTick,
							GetBridge().GetTick());
					}
					GetBridge().SubmitScheduledMoveOrder(
						UnitIds,
						Event.result.order.target_x,
						Event.result.order.target_y,
						Event.result.order.is_next,
						Event.result.order.sim_player_id,
						Event.result.order.order_id,
						ScheduledTick);
				}
				const int32 LocalPlayer = SimPlayerIdFromLogin(Comms.PlayerId());
				if (Event.result.order.sim_player_id != LocalPlayer)
				{
					ComparePeerHash(Event.result.order.hash_tick, Event.result.order.state_hash);
				}
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
		else if (Event.kind == SimRTS::CommsEventKind::StartRoom && Event.result.ok)
		{
			SnapshotSeatedPlayers(Event.result.room.player_ids);
		}
		else if (Event.kind == SimRTS::CommsEventKind::LeaveRoom && Event.result.ok)
		{
			JoinedMatchmakingRoomId.Empty();
			ResetHashHistory();
			bKickoffArmed = false;
			PendingKickoffRemainingMs = -1;
			PendingKickoffId = 0;
			ArmedKickoffId = 0;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(KickoffWaitHandle);
			}
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
	case SimRTS::CommsEventKind::StartRoom:
		View.Kind = ESimRTSCommsKind::StartRoom;
		break;
	case SimRTS::CommsEventKind::Order:
		View.Kind = ESimRTSCommsKind::JoinRoom;
		break;
	case SimRTS::CommsEventKind::Kickoff:
		View.Kind = ESimRTSCommsKind::Kickoff;
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
