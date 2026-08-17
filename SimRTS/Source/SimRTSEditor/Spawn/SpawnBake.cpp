#include "SpawnBake.h"

#include "SimRTSSpawnMarker.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	int32 NormalizeYawDegrees(float Yaw)
	{
		int32 Degrees = FMath::RoundToInt(Yaw);
		Degrees %= 360;
		if (Degrees < 0)
		{
			Degrees += 360;
		}
		return Degrees;
	}

	bool WorldToGrid(
		const FVector& WorldLocation,
		int32 Width,
		int32 Height,
		float GridScale,
		int32& OutX,
		int32& OutY)
	{
		if (GridScale <= KINDA_SMALL_NUMBER || Width <= 0 || Height <= 0)
		{
			return false;
		}

		const float WorldW = static_cast<float>(Width) * GridScale;
		const float WorldH = static_cast<float>(Height) * GridScale;
		const int32 GridX = FMath::FloorToInt((WorldLocation.X + WorldW * 0.5f) / GridScale);
		const int32 GridY = FMath::FloorToInt((WorldLocation.Y + WorldH * 0.5f) / GridScale);
		OutX = FMath::Clamp(GridX, 0, Width - 1);
		OutY = FMath::Clamp(GridY, 0, Height - 1);
		return true;
	}

	bool ReadWorldSize(const FString& LevelJsonPath, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *LevelJsonPath))
		{
			OutError = FString::Printf(TEXT("Could not read level JSON '%s'."), *LevelJsonPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("Invalid level JSON document.");
			return false;
		}

		const TSharedPtr<FJsonObject>* WorldObj = nullptr;
		if (!Root->TryGetObjectField(TEXT("world"), WorldObj) || WorldObj == nullptr || !WorldObj->IsValid())
		{
			OutError = TEXT("Level JSON missing world object.");
			return false;
		}

		double WidthNumber = 0.0;
		double HeightNumber = 0.0;
		if (!(*WorldObj)->TryGetNumberField(TEXT("width"), WidthNumber)
			|| !(*WorldObj)->TryGetNumberField(TEXT("height"), HeightNumber)
			|| WidthNumber <= 0.0
			|| HeightNumber <= 0.0)
		{
			OutError = TEXT("Level world requires positive width and height.");
			return false;
		}

		OutWidth = static_cast<int32>(WidthNumber);
		OutHeight = static_cast<int32>(HeightNumber);
		return true;
	}

	struct FSpawnBakeEntry
	{
		ASimRTSSpawnMarker* Marker = nullptr;
		int32 RequestedId = 0;
		FString ActorLabel;
	};
}

FSpawnBakeResult SpawnBake::BakeToJsonFile(const FSpawnBakeParams& Params)
{
	FSpawnBakeResult Result;

	if (Params.LevelJsonPath.IsEmpty())
	{
		Result.Error = TEXT("No level JSON path selected.");
		return Result;
	}
	if (Params.SpawnsJsonPath.IsEmpty())
	{
		Result.Error = TEXT("No spawns JSON path selected.");
		return Result;
	}
	if (Params.GridScale <= KINDA_SMALL_NUMBER)
	{
		Result.Error = TEXT("GridScale must be positive.");
		return Result;
	}

	FString SizeError;
	if (!ReadWorldSize(Params.LevelJsonPath, Result.Width, Result.Height, SizeError))
	{
		Result.Error = SizeError;
		return Result;
	}

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		Result.Error = TEXT("No editor world available.");
		return Result;
	}

	TArray<FSpawnBakeEntry> Entries;
	for (TActorIterator<ASimRTSSpawnMarker> It(World); It; ++It)
	{
		ASimRTSSpawnMarker* Marker = *It;
		if (!IsValid(Marker))
		{
			continue;
		}

		FSpawnBakeEntry Entry;
		Entry.Marker = Marker;
		Entry.RequestedId = Marker->GetSpawnId();
		Entry.ActorLabel = Marker->GetActorNameOrLabel();
		Entries.Add(Entry);
	}

	Entries.Sort([](const FSpawnBakeEntry& A, const FSpawnBakeEntry& B)
	{
		if (A.RequestedId != B.RequestedId)
		{
			// Explicit ids first (ascending), then autos (0).
			const bool AAuto = A.RequestedId <= 0;
			const bool BAuto = B.RequestedId <= 0;
			if (AAuto != BAuto)
			{
				return !AAuto && BAuto;
			}
			if (!AAuto && !BAuto)
			{
				return A.RequestedId < B.RequestedId;
			}
		}
		return A.ActorLabel < B.ActorLabel;
	});

	TSet<int32> UsedIds;
	for (const FSpawnBakeEntry& Entry : Entries)
	{
		if (Entry.RequestedId <= 0)
		{
			continue;
		}
		if (UsedIds.Contains(Entry.RequestedId))
		{
			Result.Error = FString::Printf(
				TEXT("Duplicate SpawnId %d on marker '%s'."),
				Entry.RequestedId,
				*Entry.ActorLabel);
			return Result;
		}
		UsedIds.Add(Entry.RequestedId);
	}

	int32 NextAutoId = 1;
	auto AllocateId = [&](int32 Requested) -> int32
	{
		if (Requested > 0)
		{
			return Requested;
		}
		while (UsedIds.Contains(NextAutoId))
		{
			++NextAutoId;
		}
		const int32 Id = NextAutoId++;
		UsedIds.Add(Id);
		return Id;
	};

	TArray<TSharedPtr<FJsonValue>> SpawnsArray;
	SpawnsArray.Reserve(Entries.Num());

	for (const FSpawnBakeEntry& Entry : Entries)
	{
		ASimRTSSpawnMarker* Marker = Entry.Marker;
		int32 GridX = 0;
		int32 GridY = 0;
		if (!WorldToGrid(Marker->GetActorLocation(), Result.Width, Result.Height, Params.GridScale, GridX, GridY))
		{
			Result.Error = FString::Printf(TEXT("Failed to convert marker '%s' to grid."), *Entry.ActorLabel);
			return Result;
		}

		const int32 Id = AllocateId(Entry.RequestedId);
		const int32 Rotation = NormalizeYawDegrees(Marker->GetActorRotation().Yaw);

		TSharedRef<FJsonObject> SpawnObj = MakeShared<FJsonObject>();
		SpawnObj->SetNumberField(TEXT("id"), Id);
		SpawnObj->SetStringField(TEXT("type"), Marker->GetUnitTypeJsonName());
		SpawnObj->SetNumberField(TEXT("x"), GridX);
		SpawnObj->SetNumberField(TEXT("y"), GridY);
		SpawnObj->SetNumberField(TEXT("rotation"), Rotation);
		SpawnsArray.Add(MakeShared<FJsonValueObject>(SpawnObj));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("spawns"), SpawnsArray);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		Result.Error = TEXT("Failed to serialize spawns JSON.");
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(Output, *Params.SpawnsJsonPath))
	{
		Result.Error = FString::Printf(TEXT("Could not write '%s'."), *Params.SpawnsJsonPath);
		return Result;
	}

	Result.SpawnCount = SpawnsArray.Num();
	Result.bSuccess = true;
	return Result;
}
