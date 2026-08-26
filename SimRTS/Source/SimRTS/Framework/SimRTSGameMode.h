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

USTRUCT()
struct FDelayedMoveOrder
{
	GENERATED_BODY()

	TArray<int32> UnitIds;
	int32 TargetX = 0;
	int32 TargetY = 0;
	bool bIsNext = false;
	int32 SendAtTick = 0;
};

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

	/** Queue a move for the UDP relay after mock_tick_lag ticks. Applied when the order bounces back. */
	void SubmitMoveOrder(const TArray<int32>& UnitIds, int32 TargetX, int32 TargetY, bool bIsNext);
	void FlushDelayedMoveOrders();

	bool IsMatchmakingLoggedIn() const;
	FString GetMatchmakingNickname() const;
	FString GetMatchmakingPlayerId() const;
	FString GetJoinedMatchmakingRoomId() const { return JoinedMatchmakingRoomId; }
	int32 GetMinRttMs() const;

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
	void HandleKickoff(uint32 KickoffId, int32 RemainingMs);
	void ArmSimClock();
	FSimRTSCommsEventView MakeCommsView(const SimRTS::CommsEvent& Event) const;

	UFUNCTION()
	void OnKickoffWaitElapsed();

	UPROPERTY()
	TObjectPtr<USimRTSRoom> Room;

	SimRTS::CommsClient Comms;
	int32 MockTickLag = 0;
	TArray<FDelayedMoveOrder> DelayedMoveOrders;
	FString JoinedMatchmakingRoomId;
	bool bMatchmakingMenuOpen = false;
	bool bUserRequestPending = false;
	bool bKickoffArmed = false;
	int32 PendingKickoffRemainingMs = -1;
	uint32 PendingKickoffId = 0;
	uint32 ArmedKickoffId = 0;
	float RoomListPollAccum = 0.f;
	FTimerHandle KickoffWaitHandle;
};
