#include "LevelLoader.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {

bool ParseUnitType(const FString& Name, SimRTS::UnitType& OutType, FString& OutError)
{
	if (Name.Equals(TEXT("Soldier"), ESearchCase::CaseSensitive))
	{
		OutType = SimRTS::UnitType::Soldier;
		return true;
	}
	if (Name.Equals(TEXT("Vehicle"), ESearchCase::CaseSensitive))
	{
		OutType = SimRTS::UnitType::Vehicle;
		return true;
	}
	OutError = FString::Printf(TEXT("unknown unit type '%s' (expected Soldier or Vehicle)"), *Name);
	return false;
}

bool ParseUnitDef(const TSharedPtr<FJsonObject>& Obj, SimRTS::UnitDef& OutDef, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("unit_def entry is not an object");
		return false;
	}

	FString TypeName;
	if (!Obj->TryGetStringField(TEXT("type"), TypeName) || !ParseUnitType(TypeName, OutDef.type, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("unit_def missing type");
		}
		return false;
	}

	double Speed = 0.0;
	double Diameter = 0.0;
	if (!Obj->TryGetNumberField(TEXT("speed"), Speed)
		|| !Obj->TryGetNumberField(TEXT("diameter"), Diameter))
	{
		OutError = TEXT("unit_def missing speed or diameter");
		return false;
	}

	OutDef.speed = static_cast<int32_t>(Speed);
	OutDef.diameter = static_cast<int32_t>(Diameter);
	if (OutDef.speed <= 0)
	{
		OutError = TEXT("unit_def speed must be a positive integer (points per second)");
		return false;
	}
	if (OutDef.diameter <= 0)
	{
		OutError = TEXT("unit_def diameter must be a positive integer (points, 1 point = 1 dm)");
		return false;
	}

	// Optional; default true when omitted.
	OutDef.sparse_goals = true;
	OutDef.idle_push = true;
	OutDef.weight = 1;
	bool SparseGoals = true;
	bool IdlePush = true;
	if (Obj->TryGetBoolField(TEXT("sparse_goals"), SparseGoals))
	{
		OutDef.sparse_goals = SparseGoals;
	}
	if (Obj->TryGetBoolField(TEXT("idle_push"), IdlePush))
	{
		OutDef.idle_push = IdlePush;
	}
	double Weight = 0.0;
	if (Obj->TryGetNumberField(TEXT("weight"), Weight))
	{
		OutDef.weight = static_cast<int32_t>(Weight);
		if (OutDef.weight <= 0)
		{
			OutError = TEXT("unit_def weight must be a positive integer");
			return false;
		}
	}
	return true;
}

bool ParseSpawn(const TSharedPtr<FJsonObject>& Obj, SimRTS::LevelUnitSpawn& OutSpawn, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("spawn entry is not an object");
		return false;
	}

	double Id = 0.0;
	double X = 0.0;
	double Y = 0.0;
	FString TypeName;
	if (!Obj->TryGetNumberField(TEXT("id"), Id)
		|| !Obj->TryGetStringField(TEXT("type"), TypeName)
		|| !Obj->TryGetNumberField(TEXT("x"), X)
		|| !Obj->TryGetNumberField(TEXT("y"), Y))
	{
		OutError = TEXT("spawn missing required fields (id, type, x, y)");
		return false;
	}

	if (!ParseUnitType(TypeName, OutSpawn.type, OutError))
	{
		return false;
	}

	OutSpawn.id = static_cast<SimRTS::UnitId>(Id);
	OutSpawn.position.x = static_cast<int32_t>(X);
	OutSpawn.position.y = static_cast<int32_t>(Y);

	double Rotation = 0.0;
	if (Obj->TryGetNumberField(TEXT("rotation"), Rotation))
	{
		OutSpawn.rotation = static_cast<int32_t>(Rotation);
	}

	return true;
}

bool DeserializeRoot(const FString& JsonText, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
{
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
	{
		OutError = TEXT("invalid JSON document");
		return false;
	}
	return true;
}

FLevelLoadResult Fail(const FString& Error)
{
	FLevelLoadResult Result;
	Result.Error = Error;
	return Result;
}

FLevelLoadResult LoadFileToString(const FString& FilePath, FString& OutText)
{
	if (!FFileHelper::LoadFileToString(OutText, *FilePath))
	{
		return Fail(FString::Printf(TEXT("could not read JSON '%s'"), *FilePath));
	}
	FLevelLoadResult Ok;
	Ok.bSuccess = true;
	return Ok;
}

} // namespace

FLevelLoadResult LevelLoader::ParseLevelJsonString(const FString& JsonText)
{
	FLevelLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	if (!DeserializeRoot(JsonText, Root, Result.Error))
	{
		return Result;
	}

	const TSharedPtr<FJsonObject>* WorldObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("world"), WorldObj) || WorldObj == nullptr || !WorldObj->IsValid())
	{
		Result.Error = TEXT("level: missing world object");
		return Result;
	}

	double Width = 0.0;
	double Height = 0.0;
	if (!(*WorldObj)->TryGetNumberField(TEXT("width"), Width)
		|| !(*WorldObj)->TryGetNumberField(TEXT("height"), Height)
		|| Width <= 0.0
		|| Height <= 0.0)
	{
		Result.Error = TEXT("level: world requires positive width and height");
		return Result;
	}

	Result.Level.static_data.world_width = static_cast<int32_t>(Width);
	Result.Level.static_data.world_height = static_cast<int32_t>(Height);

	// Compact linear obstruction map: index i → x = i % width, y = i / width.
	// '0' = walkable, '1' = blocked. Parsed into PathingGrid.blocked; string is not kept.
	// obstruction_distance is filled later in TickEngine::LoadLevel (engine clearance pass).
	FString Obstruction;
	if (!Root->TryGetStringField(TEXT("obstruction"), Obstruction))
	{
		Result.Error = TEXT("level: missing obstruction string");
		return Result;
	}

	const int32_t WorldW = Result.Level.static_data.world_width;
	const int32_t WorldH = Result.Level.static_data.world_height;
	const int64 ExpectedLen = static_cast<int64>(WorldW) * static_cast<int64>(WorldH);
	if (Obstruction.Len() != ExpectedLen)
	{
		Result.Error = FString::Printf(
			TEXT("level: obstruction length %d does not match world size %d x %d (%lld)"),
			Obstruction.Len(),
			WorldW,
			WorldH,
			ExpectedLen);
		return Result;
	}

	SimRTS::PathingGrid& Pathing = Result.Level.static_data.pathing;
	Pathing.ResizeOpen(WorldW, WorldH);
	for (int32 Index = 0; Index < Obstruction.Len(); ++Index)
	{
		const TCHAR Ch = Obstruction[Index];
		if (Ch != TCHAR('0') && Ch != TCHAR('1'))
		{
			Result.Error = FString::Printf(
				TEXT("level: obstruction contains invalid character at index %d (expected '0' or '1')"),
				Index);
			return Result;
		}

		const int32_t X = Index % WorldW;
		const int32_t Y = Index / WorldW;
		Pathing.At(X, Y).blocked = (Ch == TCHAR('1'));
	}

	Result.bSuccess = true;
	return Result;
}

FLevelLoadResult LevelLoader::ParseRulesJsonString(const FString& JsonText)
{
	FLevelLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	if (!DeserializeRoot(JsonText, Root, Result.Error))
	{
		return Result;
	}

	double TicksPerSecond = 0.0;
	if (!Root->TryGetNumberField(TEXT("ticks_per_second"), TicksPerSecond) || TicksPerSecond <= 0.0)
	{
		Result.Error = TEXT("rules: requires positive ticks_per_second");
		return Result;
	}
	Result.Level.static_data.ticks_per_second = static_cast<int32_t>(TicksPerSecond);

	const TArray<TSharedPtr<FJsonValue>>* UnitDefs = nullptr;
	if (!Root->TryGetArrayField(TEXT("unit_defs"), UnitDefs) || UnitDefs == nullptr)
	{
		Result.Error = TEXT("rules: missing unit_defs array");
		return Result;
	}

	for (const TSharedPtr<FJsonValue>& Entry : *UnitDefs)
	{
		SimRTS::UnitDef Def;
		if (!ParseUnitDef(Entry->AsObject(), Def, Result.Error))
		{
			Result.Error = TEXT("rules: ") + Result.Error;
			return Result;
		}
		const size_t Index = static_cast<size_t>(Def.type);
		if (Index >= Result.Level.static_data.unit_defs.size())
		{
			Result.Error = TEXT("rules: unit_def type index out of range");
			return Result;
		}
		Result.Level.static_data.unit_defs[Index] = Def;
	}

	Result.bSuccess = true;
	return Result;
}

FLevelLoadResult LevelLoader::ParseSpawnsJsonString(const FString& JsonText)
{
	FLevelLoadResult Result;

	TSharedPtr<FJsonObject> Root;
	if (!DeserializeRoot(JsonText, Root, Result.Error))
	{
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
	if (!Root->TryGetArrayField(TEXT("spawns"), Spawns) || Spawns == nullptr)
	{
		Result.Error = TEXT("spawns: missing spawns array");
		return Result;
	}

	for (const TSharedPtr<FJsonValue>& Entry : *Spawns)
	{
		SimRTS::LevelUnitSpawn Spawn;
		if (!ParseSpawn(Entry->AsObject(), Spawn, Result.Error))
		{
			Result.Error = TEXT("spawns: ") + Result.Error;
			return Result;
		}
		Result.Level.spawns.push_back(Spawn);
	}

	Result.bSuccess = true;
	return Result;
}

FLevelLoadResult LevelLoader::MergeParts(
	const FLevelLoadResult& LevelPart,
	const FLevelLoadResult& RulesPart,
	const FLevelLoadResult& SpawnsPart)
{
	if (!LevelPart.bSuccess)
	{
		return LevelPart;
	}
	if (!RulesPart.bSuccess)
	{
		return RulesPart;
	}
	if (!SpawnsPart.bSuccess)
	{
		return SpawnsPart;
	}

	FLevelLoadResult Result;
	Result.Level = LevelPart.Level;
	Result.Level.static_data.ticks_per_second = RulesPart.Level.static_data.ticks_per_second;
	Result.Level.static_data.unit_defs = RulesPart.Level.static_data.unit_defs;
	Result.Level.spawns = SpawnsPart.Level.spawns;
	Result.bSuccess = true;
	return Result;
}

FLevelLoadResult LevelLoader::LoadFromFiles(
	const FString& LevelFilePath,
	const FString& RulesFilePath,
	const FString& SpawnsFilePath)
{
	FString LevelText;
	FString RulesText;
	FString SpawnsText;

	FLevelLoadResult FileResult = LoadFileToString(LevelFilePath, LevelText);
	if (!FileResult.bSuccess)
	{
		return FileResult;
	}
	FileResult = LoadFileToString(RulesFilePath, RulesText);
	if (!FileResult.bSuccess)
	{
		return FileResult;
	}
	FileResult = LoadFileToString(SpawnsFilePath, SpawnsText);
	if (!FileResult.bSuccess)
	{
		return FileResult;
	}

	return MergeParts(
		ParseLevelJsonString(LevelText),
		ParseRulesJsonString(RulesText),
		ParseSpawnsJsonString(SpawnsText));
}
