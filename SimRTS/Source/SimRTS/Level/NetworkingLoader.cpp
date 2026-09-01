#include "NetworkingLoader.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FNetworkingLoadResult NetworkingLoader::ParseJsonString(const FString& JsonText)
{
	FNetworkingLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Error = TEXT("invalid JSON document");
		return Result;
	}

	FString Ip;
	if (!Root->TryGetStringField(TEXT("ip"), Ip) || Ip.IsEmpty())
	{
		Result.Error = TEXT("networking: missing ip");
		return Result;
	}

	double PortNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("port"), PortNumber))
	{
		Result.Error = TEXT("networking: missing port");
		return Result;
	}
	const int32 Port = static_cast<int32>(PortNumber);
	if (Port < 1 || Port > 65535)
	{
		Result.Error = TEXT("networking: port must be 1-65535");
		return Result;
	}

	double UdpPortNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("udp_port"), UdpPortNumber))
	{
		Result.Error = TEXT("networking: missing udp_port");
		return Result;
	}
	const int32 UdpPort = static_cast<int32>(UdpPortNumber);
	if (UdpPort < 1 || UdpPort > 65535)
	{
		Result.Error = TEXT("networking: udp_port must be 1-65535");
		return Result;
	}

	double FutureTickNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("future_tick_distance"), FutureTickNumber))
	{
		Result.Error = TEXT("networking: missing future_tick_distance");
		return Result;
	}
	const int32 FutureTickDistance = static_cast<int32>(FutureTickNumber);
	if (FutureTickDistance < 1)
	{
		Result.Error = TEXT("networking: future_tick_distance must be >= 1");
		return Result;
	}

	double PingIntervalNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("ping_interval_ms"), PingIntervalNumber))
	{
		Result.Error = TEXT("networking: missing ping_interval_ms");
		return Result;
	}
	const int32 PingIntervalMs = static_cast<int32>(PingIntervalNumber);
	if (PingIntervalMs < 1)
	{
		Result.Error = TEXT("networking: ping_interval_ms must be >= 1");
		return Result;
	}

	double PingKeepNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("ping_keep_amount"), PingKeepNumber))
	{
		Result.Error = TEXT("networking: missing ping_keep_amount");
		return Result;
	}
	const int32 PingKeepAmount = static_cast<int32>(PingKeepNumber);
	if (PingKeepAmount < 1)
	{
		Result.Error = TEXT("networking: ping_keep_amount must be >= 1");
		return Result;
	}

	double PacerMinDelayNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("pacer_min_delay_ms"), PacerMinDelayNumber))
	{
		Result.Error = TEXT("networking: missing pacer_min_delay_ms");
		return Result;
	}
	const int32 PacerMinDelayMs = static_cast<int32>(PacerMinDelayNumber);
	if (PacerMinDelayMs < 1)
	{
		Result.Error = TEXT("networking: pacer_min_delay_ms must be >= 1");
		return Result;
	}

	double ClickKeepNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("click_keep_ms"), ClickKeepNumber))
	{
		Result.Error = TEXT("networking: missing click_keep_ms");
		return Result;
	}
	const int32 ClickKeepMs = static_cast<int32>(ClickKeepNumber);
	if (ClickKeepMs < 1)
	{
		Result.Error = TEXT("networking: click_keep_ms must be >= 1");
		return Result;
	}

	Result.Config.Ip = Ip;
	Result.Config.Port = Port;
	Result.Config.UdpPort = UdpPort;
	Result.Config.FutureTickDistance = FutureTickDistance;
	Result.Config.PingIntervalMs = PingIntervalMs;
	Result.Config.PingKeepAmount = PingKeepAmount;
	Result.Config.PacerMinDelayMs = PacerMinDelayMs;
	Result.Config.ClickKeepMs = ClickKeepMs;
	Result.bSuccess = true;
	return Result;
}

FNetworkingLoadResult NetworkingLoader::LoadFromFile(const FString& FilePath)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		FNetworkingLoadResult Result;
		Result.Error = FString::Printf(TEXT("could not read JSON '%s'"), *FilePath);
		return Result;
	}
	return ParseJsonString(JsonText);
}

FNetworkingLoadResult NetworkingLoader::LoadDefault()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Networking.json");
	return LoadFromFile(Path);
}
