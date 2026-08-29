#pragma once

#include "CoreMinimal.h"
#include "CommsClient.h"
#include "GameFramework/GameModeBase.h"
#include "SimRTSCommsView.h"
#include "SimRTSRoom.h"
#include "SimRTSGameMode.generated.h"

class ASimRTSUnitActor;
class UUnitViewManager;
class ASimRTSObstructionGridVisualizer;
class ASimRTSPathVisualizer;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimRTSCommsEvent, const FSimRTSCommsEventView&);

UCLASS()
class SIMRTS_API ASimRTSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimRTSGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Load the default sim room (level, units, sim clock). No-op if already loaded. */
	bool StartDefaultRoom();

	bool IsRoomLoaded() const { return Room != nullptr && Room->IsLoaded(); }

	FSimBridge& GetBridge() { return Room->GetBridge(); }
	const FSimBridge& GetBridge() const { return Room->GetBridge(); }

	UUnitViewManager* GetUnitViewManager() const { return Room != nullptr ? Room->GetUnitViewManager() : nullptr; }

	float GetGridScale() const { return GridScale; }

	FVector GridToWorld(int32 X, int32 Y) const;
	bool WorldToGrid(const FVector& WorldLocation, int32& OutX, int32& OutY) const;

	void RequestLogin(const FString& Username);
	void RequestGetRooms();
	void RequestCreateRoom(const FString& RoomId);
	void RequestJoinRoom(const FString& RoomId);
	void RequestLeaveRoom();
	void RequestStartRoom();

	void SetMatchmakingMenuOpen(bool bOpen);

	/** Send a move to the UDP relay. Applied from the bounce at actual_tick + future_tick_distance. */
	void SubmitMoveOrder(const TArray<int32>& UnitIds, int32 TargetX, int32 TargetY, bool bIsNext);

	bool IsMatchmakingLoggedIn() const;
	FString GetMatchmakingNickname() const;
	FString GetMatchmakingPlayerId() const;
	FString GetJoinedMatchmakingRoomId() const { return JoinedMatchmakingRoomId; }
	int32 GetMinRttMs() const;

	bool IsSimClockStarted() const { return Room != nullptr && Room->IsClockStarted(); }
	bool AreTickHaltKeysEnabled() const { return bEnableTickHaltKeys; }
	float GetMinTickDelaySeconds() const { return MinTickDelaySeconds; }
	void SetSimTickHalted(bool bHalted);
	bool IsSimTickHalted() const;
	int32 GetSimTicksBehind() const;
	int32 GetActualTick() const;
	bool IsDesynced() const;
	bool IsSimLocked() const;
	bool HasAllCommandsForSimTick() const;
	const TArray<int32>& GetSeatedSimPlayerIds() const { return SeatedSimPlayerIds; }

	void RecordGameplayHash();

	FOnSimRTSCommsEvent OnCommsEvent;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "SimRTS")
	float GridScale = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Comms", meta = (ClampMin = "0.5"))
	float RoomListPollSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug")
	bool bShowObstructionGrid = false;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug", meta = (ClampMin = "1"))
	int32 ObstructionGridMaxBlueDistance = 30;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug")
	bool bShowUnitPaths = true;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Debug")
	bool bEnableTickHaltKeys = true;

	UPROPERTY()
	TObjectPtr<ASimRTSObstructionGridVisualizer> ObstructionGridVisualizer;

	UPROPERTY()
	TObjectPtr<ASimRTSPathVisualizer> PathVisualizer;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Units")
	TSoftClassPtr<ASimRTSUnitActor> SoldierActorClass;

	UPROPERTY(EditDefaultsOnly, Category = "SimRTS|Units")
	TSoftClassPtr<ASimRTSUnitActor> VehicleActorClass;

private:
	void SpawnDebugVisualizers();
	void DestroyDebugVisualizers();
	void PumpComms();
	void MaybePollRooms(float DeltaSeconds);
	void MaybeSendEmptyOrders();
	void HandleKickoff(uint32 KickoffId, int32 RemainingMs);
	void ArmSimClock();
	void ResetHashHistory();
	void SnapshotSeatedPlayers(const std::vector<std::string>& PlayerIds);
	void NoteCommandFrame(int32 SimPlayerId, int32 ActualTick);
	void PruneCommandFrames();
	void LatestOrderHash(int32& OutHashTick, uint64& OutStateHash) const;
	void ComparePeerHash(int32 HashTick, uint64 StateHash);
	bool TryGetLocalHash(int32 Tick, uint64& OutHash) const;
	FSimRTSCommsEventView MakeCommsView(const SimRTS::CommsEvent& Event) const;

	UFUNCTION()
	void OnKickoffWaitElapsed();

	UPROPERTY()
	TObjectPtr<USimRTSRoom> Room;

	SimRTS::CommsClient Comms;
	int32 FutureTickDistance = 5;
	float MinTickDelaySeconds = 0.01f;
	uint32 NextOrderId = 1;
	int32 LastCoveredActualTick = -1;
	TSet<int32> ActualTicksWithClicks;
	TArray<int32> SeatedSimPlayerIds;
	TMap<int32, TSet<int32>> CommandFramesByPlayer;
	FString JoinedMatchmakingRoomId;
	bool bDesynced = false;
	TArray<TPair<int32, uint64>> LocalHashes;
	TArray<TPair<int32, uint64>> PendingRemoteHashes;
	bool bMatchmakingMenuOpen = false;
	bool bUserRequestPending = false;
	bool bKickoffArmed = false;
	int32 PendingKickoffRemainingMs = -1;
	uint32 PendingKickoffId = 0;
	uint32 ArmedKickoffId = 0;
	float RoomListPollAccum = 0.f;
	FTimerHandle KickoffWaitHandle;
};
