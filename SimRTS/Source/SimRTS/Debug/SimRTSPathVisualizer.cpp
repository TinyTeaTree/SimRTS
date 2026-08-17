#include "SimRTSPathVisualizer.h"

#include "BattleState.h"
#include "SimRTSGameMode.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

namespace
{
	constexpr float kPathDrawZ = 25.f;
	constexpr float kDashSize = 30.f;
	constexpr float kArrowSize = 50.f;
	constexpr float kArrowThickness = 2.f;

	bool OrderContainsUnit(const SimRTS::Order& Order, SimRTS::UnitId Id)
	{
		for (const SimRTS::UnitId Candidate : Order.unit_ids)
		{
			if (Candidate == Id)
			{
				return true;
			}
		}
		return false;
	}

	FColor ColorForUnit(SimRTS::UnitId Id, bool bActiveSegment)
	{
		const uint8 Hue = static_cast<uint8>((Id * 47) % 256);
		const uint8 Value = bActiveSegment ? 255 : 170;
		const uint8 Saturation = bActiveSegment ? 220 : 160;
		return FLinearColor::MakeFromHSV8(Hue, Saturation, Value).ToFColor(true);
	}

	void DrawDashedLine(UWorld* World, const FVector& Start, const FVector& End, const FColor& Color)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length < 1.f)
		{
			return;
		}

		const FVector Dir = Delta / Length;
		float Drawn = 0.f;
		bool bDashOn = true;
		while (Drawn < Length)
		{
			const float SegEnd = FMath::Min(Drawn + kDashSize, Length);
			if (bDashOn)
			{
				DrawDebugLine(
					World,
					Start + Dir * Drawn,
					Start + Dir * SegEnd,
					Color,
					false,
					0.f,
					SDPG_World,
					kArrowThickness);
			}
			Drawn = SegEnd;
			bDashOn = !bDashOn;
		}
	}

	void DrawDashedArrow(UWorld* World, const FVector& Start, const FVector& End, const FColor& Color)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length < 1.f)
		{
			return;
		}

		const FVector Dir = Delta / Length;
		const float Head = FMath::Min(kArrowSize, Length * 0.35f);
		const FVector Neck = End - Dir * Head;

		DrawDashedLine(World, Start, Neck, Color);
		DrawDebugDirectionalArrow(World, Neck, End, Head, Color, false, 0.f, SDPG_World, kArrowThickness);
	}

	FVector ToWorld(const ASimRTSGameMode& GameMode, const SimRTS::Vec2i& Point)
	{
		FVector World = GameMode.GridToWorld(Point.x, Point.y);
		World.Z = kPathDrawZ;
		return World;
	}
}

ASimRTSPathVisualizer::ASimRTSPathVisualizer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bIsEditorOnlyActor = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SetActorEnableCollision(false);
}

void ASimRTSPathVisualizer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const ASimRTSGameMode* GameMode = World->GetAuthGameMode<ASimRTSGameMode>();
	if (GameMode == nullptr)
	{
		return;
	}

	DrawUnitPaths(*GameMode);
}

void ASimRTSPathVisualizer::DrawUnitPaths(const ASimRTSGameMode& GameMode) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const SimRTS::BattleState& State = GameMode.GetBridge().GetState();
	for (const SimRTS::Unit& Unit : State.units)
	{
		SimRTS::Vec2i Cursor = Unit.position;
		if (Unit.move.active)
		{
			DrawDashedArrow(
				World,
				ToWorld(GameMode, Unit.move.start),
				ToWorld(GameMode, Unit.move.end),
				ColorForUnit(Unit.id, true));
			Cursor = Unit.move.end;
		}

		for (const SimRTS::Order& Order : State.orders)
		{
			if (Order.type != SimRTS::OrderType::Move || !OrderContainsUnit(Order, Unit.id))
			{
				continue;
			}

			DrawDashedArrow(
				World,
				ToWorld(GameMode, Cursor),
				ToWorld(GameMode, Order.target),
				ColorForUnit(Unit.id, false));
			Cursor = Order.target;
		}
	}
}
