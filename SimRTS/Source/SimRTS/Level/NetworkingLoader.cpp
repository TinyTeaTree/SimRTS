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

	double LagNumber = 0.0;
	if (!Root->TryGetNumberField(TEXT("mock_tick_lag"), LagNumber))
	{
		Result.Error = TEXT("networking: missing mock_tick_lag");
		return Result;
	}
	const int32 MockTickLag = static_cast<int32>(LagNumber);
	if (MockTickLag < 0)
	{
		Result.Error = TEXT("networking: mock_tick_lag must be >= 0");
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

	Result.Config.Ip = Ip;
	Result.Config.Port = Port;
	Result.Config.UdpPort = UdpPort;
	Result.Config.MockTickLag = MockTickLag;
	Result.Config.PingIntervalMs = PingIntervalMs;
	Result.Config.PingKeepAmount = PingKeepAmount;
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
