#pragma once

#include "CoreMinimal.h"

struct FNetworkingConfig
{
	FString Ip;
	int32 Port = 0;
	int32 UdpPort = 0;
	int32 FutureTickDistance = 5;
	int32 PingIntervalMs = 200;
	int32 PingKeepAmount = 10;
	int32 PacerMinDelayMs = 10;
	int32 ClickKeepMs = 10000;
};

struct FNetworkingLoadResult
{
	bool bSuccess = false;
	FNetworkingConfig Config;
	FString Error;
};

/**
 * Parse Networking.json (Unreal Json module). Required fields: ip, port, udp_port, future_tick_distance, ping_interval_ms, ping_keep_amount, pacer_min_delay_ms, click_keep_ms.
 */
namespace NetworkingLoader
{
	FNetworkingLoadResult ParseJsonString(const FString& JsonText);
	FNetworkingLoadResult LoadFromFile(const FString& FilePath);
	FNetworkingLoadResult LoadDefault();
}
