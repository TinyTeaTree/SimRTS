#pragma once

#include "CoreMinimal.h"

struct FNetworkingConfig
{
	FString Ip;
	int32 Port = 0;
	int32 UdpPort = 0;
	int32 MockTickLag = 0;
};

struct FNetworkingLoadResult
{
	bool bSuccess = false;
	FNetworkingConfig Config;
	FString Error;
};

/**
 * Parse Networking.json (Unreal Json module). Required fields: ip, port, udp_port, mock_tick_lag.
 */
namespace NetworkingLoader
{
	FNetworkingLoadResult ParseJsonString(const FString& JsonText);
	FNetworkingLoadResult LoadFromFile(const FString& FilePath);
	FNetworkingLoadResult LoadDefault();
}
