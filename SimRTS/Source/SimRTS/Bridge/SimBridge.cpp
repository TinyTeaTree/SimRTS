#include "SimBridge.h"

#include "LevelLoader.h"
#include "Misc/Paths.h"

namespace {

FString DefaultLevelJsonPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Levels/DefaultLevel.json");
}

FString DefaultGameRulesJsonPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/GameRules.json");
}

FString DefaultSpawnsJsonPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Levels/DefaultSpawns.json");
}

} // namespace

bool FSimBridge::ResetToDefaultLevel()
{
	const FString LevelPath = DefaultLevelJsonPath();
	const FString RulesPath = DefaultGameRulesJsonPath();
	const FString SpawnsPath = DefaultSpawnsJsonPath();

	const FLevelLoadResult Loaded = LevelLoader::LoadFromFiles(LevelPath, RulesPath, SpawnsPath);
	if (!Loaded.bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load level data (level='%s', rules='%s', spawns='%s'): %s"),
			*LevelPath,
			*RulesPath,
			*SpawnsPath,
			*Loaded.Error);
		return false;
	}

	Engine.LoadLevel(Loaded.Level);
	UE_LOG(LogTemp, Log, TEXT("Loaded level from %s + %s + %s (units=%d, world=%dx%d)"),
		*LevelPath,
		*RulesPath,
		*SpawnsPath,
		static_cast<int32>(Loaded.Level.spawns.size()),
		Loaded.Level.static_data.world_width,
		Loaded.Level.static_data.world_height);
	return true;
}

void FSimBridge::SubmitMoveOrder(
	const TArray<int32>& UnitIds,
	int32 TargetX,
	int32 TargetY,
	bool bIsNext,
	int32 PlayerId)
{
	SimRTS::Order Order;
	Order.player_id = PlayerId;
	Order.type = SimRTS::OrderType::Move;
	Order.target = {TargetX, TargetY};
	Order.is_next = bIsNext;
	Order.unit_ids.reserve(UnitIds.Num());
	for (int32 Id : UnitIds)
	{
		Order.unit_ids.push_back(Id);
	}
	Engine.SubmitOrder(std::move(Order));
}

void FSimBridge::SubmitScheduledMoveOrder(
	const TArray<int32>& UnitIds,
	int32 TargetX,
	int32 TargetY,
	bool bIsNext,
	int32 PlayerId,
	uint32 OrderId,
	int32 ScheduledTick)
{
	SimRTS::Order Order;
	Order.player_id = PlayerId;
	Order.type = SimRTS::OrderType::Move;
	Order.target = {TargetX, TargetY};
	Order.is_next = bIsNext;
	Order.unit_ids.reserve(UnitIds.Num());
	for (int32 Id : UnitIds)
	{
		Order.unit_ids.push_back(Id);
	}
	Engine.SubmitScheduled(std::move(Order), OrderId, ScheduledTick);
}

void FSimBridge::StepForward()
{
	Engine.StepForward();
}

int32 FSimBridge::GetTick() const
{
	return Engine.GetTick();
}

const SimRTS::BattleState& FSimBridge::GetState() const
{
	return Engine.GetState();
}

const SimRTS::StaticBattleData& FSimBridge::GetStaticData() const
{
	return Engine.GetStaticData();
}
