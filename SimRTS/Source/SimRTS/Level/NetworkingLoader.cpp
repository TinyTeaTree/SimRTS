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

	Result.Config.Ip = Ip;
	Result.Config.Port = Port;
	Result.Config.MockTickLag = MockTickLag;
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
