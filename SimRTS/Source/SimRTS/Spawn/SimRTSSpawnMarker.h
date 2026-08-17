#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimRTSSpawnMarker.generated.h"

class UArrowComponent;
class USceneComponent;
class UStaticMeshComponent;

/** Unit type written into spawns JSON (`Soldier` / `Vehicle`). */
UENUM(BlueprintType)
enum class ESimRTSSpawnUnitType : uint8
{
	Soldier UMETA(DisplayName = "Soldier"),
	Vehicle UMETA(DisplayName = "Vehicle"),
};

/**
 * Editor-only spawn gizmo. Place in the level, bake into DefaultSpawns.json.
 * Stripped from gameplay (`bIsEditorOnlyActor`); runtime units still come from JSON + UnitViewManager.
 */
UCLASS(ClassGroup = SimRTS, meta = (DisplayName = "SimRTS Spawn Marker"))
class SIMRTS_API ASimRTSSpawnMarker : public AActor
{
	GENERATED_BODY()

public:
	ASimRTSSpawnMarker();

	static FName SpawnTag();

	ESimRTSSpawnUnitType GetUnitType() const { return UnitType; }

	/** Explicit spawn id for JSON. 0 = auto-assign during bake (stable order). */
	int32 GetSpawnId() const { return SpawnId; }

	FString GetUnitTypeJsonName() const;

	UFUNCTION(CallInEditor, Category = "SimRTS|Spawn")
	void RefreshPreview();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void ApplyPreviewVisuals();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Spawn")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Viewport preview only; no collision. Shape follows UnitType. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Spawn")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Faces local +X (sim yaw 0), matching unit actors. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Spawn")
	TObjectPtr<UArrowComponent> FacingArrow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimRTS|Spawn")
	ESimRTSSpawnUnitType UnitType = ESimRTSSpawnUnitType::Soldier;

	/** JSON `id`. Leave 0 to auto-number during bake. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimRTS|Spawn", meta = (ClampMin = "0"))
	int32 SpawnId = 0;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SoldierMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> VehicleMesh;
};
