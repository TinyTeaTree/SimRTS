#include "ObstructionBake.h"

#include "SimRTSObstructionCylinderVolume.h"
#include "SimRTSObstructionVolume.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	struct FVolumeBakeJob
	{
		UBoxComponent* Box = nullptr;
		UCapsuleComponent* Capsule = nullptr;
		int32 MinX = 0;
		int32 MaxX = 0;
		int32 MinY = 0;
		int32 MaxY = 0;

		int32 CellCount() const
		{
			return (MaxX - MinX + 1) * (MaxY - MinY + 1);
		}
	};

	FVector GridToWorld(int32 X, int32 Y, int32 Width, int32 Height, float GridScale)
	{
		const float WorldW = static_cast<float>(Width) * GridScale;
		const float WorldH = static_cast<float>(Height) * GridScale;
		const float XWorld = static_cast<float>(X) * GridScale - WorldW * 0.5f;
		const float YWorld = static_cast<float>(Y) * GridScale - WorldH * 0.5f;
		return FVector(XWorld, YWorld, 0.f);
	}

	bool PointInBoxFootprint(const UBoxComponent* Box, const FVector& GroundPoint)
	{
		if (Box == nullptr)
		{
			return false;
		}

		FVector TestPoint = GroundPoint;
		TestPoint.Z = Box->GetComponentLocation().Z;

		// Rotate-only: GetScaledBoxExtent is already in world units, so the point
		// must stay in world scale (InverseTransformPosition would divide it out).
		const FTransform& Xform = Box->GetComponentTransform();
		const FVector Local = Xform.GetRotation().UnrotateVector(TestPoint - Xform.GetLocation());
		const FVector Ext = Box->GetScaledBoxExtent();
		return FMath::Abs(Local.X) <= Ext.X && FMath::Abs(Local.Y) <= Ext.Y;
	}

	bool PointInCapsuleFootprint(const UCapsuleComponent* Capsule, const FVector& GroundPoint)
	{
		if (Capsule == nullptr)
		{
			return false;
		}

		// Top-down RTS: circular XY footprint from the capsule radius (supports actor scale/yaw).
		FVector TestPoint = GroundPoint;
		TestPoint.Z = Capsule->GetComponentLocation().Z;

		const FTransform& Xform = Capsule->GetComponentTransform();
		const FVector Local = Xform.GetRotation().UnrotateVector(TestPoint - Xform.GetLocation());
		const float Radius = Capsule->GetScaledCapsuleRadius();
		return (Local.X * Local.X + Local.Y * Local.Y) <= (Radius * Radius);
	}

	bool PointInJobFootprint(const FVolumeBakeJob& Job, const FVector& GroundPoint)
	{
		if (Job.Box != nullptr)
		{
			return PointInBoxFootprint(Job.Box, GroundPoint);
		}
		if (Job.Capsule != nullptr)
		{
			return PointInCapsuleFootprint(Job.Capsule, GroundPoint);
		}
		return false;
	}

	void WorldBoundsToGridRange(
		const FBox& Bounds,
		int32 Width,
		int32 Height,
		float GridScale,
		int32& OutMinX,
		int32& OutMaxX,
		int32& OutMinY,
		int32& OutMaxY)
	{
		const float WorldW = static_cast<float>(Width) * GridScale;
		const float WorldH = static_cast<float>(Height) * GridScale;

		auto ToGridX = [&](float WorldX)
		{
			return FMath::FloorToInt((WorldX + WorldW * 0.5f) / GridScale);
		};
		auto ToGridY = [&](float WorldY)
		{
			return FMath::FloorToInt((WorldY + WorldH * 0.5f) / GridScale);
		};

		OutMinX = FMath::Clamp(ToGridX(Bounds.Min.X), 0, Width - 1);
		OutMaxX = FMath::Clamp(ToGridX(Bounds.Max.X), 0, Width - 1);
		OutMinY = FMath::Clamp(ToGridY(Bounds.Min.Y), 0, Height - 1);
		OutMaxY = FMath::Clamp(ToGridY(Bounds.Max.Y), 0, Height - 1);

		if (OutMinX > OutMaxX)
		{
			Swap(OutMinX, OutMaxX);
		}
		if (OutMinY > OutMaxY)
		{
			Swap(OutMinY, OutMaxY);
		}
	}

	void AddJobFromComponent(
		USceneComponent* Component,
		UBoxComponent* Box,
		UCapsuleComponent* Capsule,
		float GridScale,
		int32 Width,
		int32 Height,
		TArray<FVolumeBakeJob>& Jobs,
		int32& TotalWork)
	{
		if (Component == nullptr)
		{
			return;
		}

		const FBox Bounds = Component->CalcBounds(Component->GetComponentTransform()).GetBox().ExpandBy(GridScale);

		FVolumeBakeJob Job;
		Job.Box = Box;
		Job.Capsule = Capsule;
		WorldBoundsToGridRange(Bounds, Width, Height, GridScale, Job.MinX, Job.MaxX, Job.MinY, Job.MaxY);
		TotalWork += Job.CellCount();
		Jobs.Add(Job);
	}
}

FObstructionBakeResult ObstructionBake::BakeToJsonFile(const FObstructionBakeParams& Params)
{
	FObstructionBakeResult Result;

	if (Params.JsonPath.IsEmpty())
	{
		Result.Error = TEXT("No JSON path selected.");
		return Result;
	}
	if (Params.GridScale <= KINDA_SMALL_NUMBER)
	{
		Result.Error = TEXT("GridScale must be positive.");
		return Result;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Params.JsonPath))
	{
		Result.Error = FString::Printf(TEXT("Could not read '%s'."), *Params.JsonPath);
		return Result;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Error = TEXT("Invalid JSON document.");
		return Result;
	}

	const TSharedPtr<FJsonObject>* WorldObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("world"), WorldObj) || WorldObj == nullptr || !WorldObj->IsValid())
	{
		Result.Error = TEXT("JSON missing world object.");
		return Result;
	}

	double WidthNumber = 0.0;
	double HeightNumber = 0.0;
	if (!(*WorldObj)->TryGetNumberField(TEXT("width"), WidthNumber)
		|| !(*WorldObj)->TryGetNumberField(TEXT("height"), HeightNumber)
		|| WidthNumber <= 0.0
		|| HeightNumber <= 0.0)
	{
		Result.Error = TEXT("world requires positive width and height.");
		return Result;
	}

	const int32 Width = static_cast<int32>(WidthNumber);
	const int32 Height = static_cast<int32>(HeightNumber);
	Result.Width = Width;
	Result.Height = Height;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		Result.Error = TEXT("No editor world available.");
		return Result;
	}

	TArray<FVolumeBakeJob> Jobs;
	int32 TotalWork = 0;
	int32 VolumeCount = 0;

	for (TActorIterator<ASimRTSObstructionVolume> It(World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		++VolumeCount;
		AddJobFromComponent(It->GetBox(), It->GetBox(), nullptr, Params.GridScale, Width, Height, Jobs, TotalWork);
	}

	for (TActorIterator<ASimRTSObstructionCylinderVolume> It(World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		++VolumeCount;
		AddJobFromComponent(It->GetCapsule(), nullptr, It->GetCapsule(), Params.GridScale, Width, Height, Jobs, TotalWork);
	}

	Result.VolumeCount = VolumeCount;

	const int64 TotalCells = static_cast<int64>(Width) * static_cast<int64>(Height);
	FString Obstruction = FString::ChrN(static_cast<int32>(TotalCells), TEXT('0'));

	FScopedSlowTask SlowTask(
		static_cast<float>(FMath::Max(1, TotalWork)),
		NSLOCTEXT("SimRTSEditor", "ObstructionBakeProgress", "Baking obstruction map..."));
	SlowTask.MakeDialog(true);

	int32 BlockedCells = 0;

	if (Jobs.Num() == 0)
	{
		SlowTask.EnterProgressFrame(1.f);
	}
	else
	{
		int32 PendingFrames = 0;
		constexpr int32 ReportEvery = 2048;

		for (const FVolumeBakeJob& Job : Jobs)
		{
			for (int32 Y = Job.MinY; Y <= Job.MaxY; ++Y)
			{
				for (int32 X = Job.MinX; X <= Job.MaxX; ++X)
				{
					if (SlowTask.ShouldCancel())
					{
						Result.Error = TEXT("Bake cancelled.");
						return Result;
					}

					++PendingFrames;
					if (PendingFrames >= ReportEvery)
					{
						SlowTask.EnterProgressFrame(static_cast<float>(PendingFrames));
						PendingFrames = 0;
					}

					const FVector Ground = GridToWorld(X, Y, Width, Height, Params.GridScale);
					if (!PointInJobFootprint(Job, Ground))
					{
						continue;
					}

					const int32 Index = Y * Width + X;
					if (Obstruction[Index] != TCHAR('1'))
					{
						Obstruction[Index] = TCHAR('1');
						++BlockedCells;
					}
				}
			}
		}

		if (PendingFrames > 0)
		{
			SlowTask.EnterProgressFrame(static_cast<float>(PendingFrames));
		}
	}

	Result.BlockedCells = BlockedCells;
	Root->SetStringField(TEXT("obstruction"), Obstruction);

	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		Result.Error = TEXT("Failed to serialize JSON.");
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(Output, *Params.JsonPath))
	{
		Result.Error = FString::Printf(TEXT("Could not write '%s'."), *Params.JsonPath);
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}
