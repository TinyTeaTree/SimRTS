#pragma once

#include "CoreMinimal.h"

enum class ESimRTSCommsKind : uint8
{
	Login,
	GetRooms,
	CreateRoom,
	JoinRoom,
	LeaveRoom
};

struct FSimRTSCommsRoomView
{
	FString Id;
	TArray<FString> PlayerIds;
};

struct FSimRTSCommsEventView
{
	ESimRTSCommsKind Kind = ESimRTSCommsKind::Login;
	bool bOk = false;
	int32 HttpStatus = 0;
	FString Error;
	FString Nickname;
	FString PlayerId;
	FSimRTSCommsRoomView Room;
	TArray<FSimRTSCommsRoomView> Rooms;
};
