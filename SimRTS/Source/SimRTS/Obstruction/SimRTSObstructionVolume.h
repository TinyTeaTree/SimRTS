#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimRTSObstructionVolume.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** Editor-authored square obstruction volume. Baked into level JSON; not sim runtime state. */
UCLASS(ClassGroup = SimRTS, meta = (DisplayName = "SimRTS Obstruction Volume"))
class SIMRTS_API ASimRTSObstructionVolume : public AActor
{
	GENERATED_BODY()

public:
	ASimRTSObstructionVolume();

	/** Stable tag used by the future bake step (`ActorHasTag`). */
	static FName ObstructionTag();

	UBoxComponent* GetBox() const { return Box; }

protected:
	/** Query volume for bake tests. Extent 50 → 100 UU cube at scale 1 (matches BasicShapes cube). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Obstruction")
	TObjectPtr<UBoxComponent> Box;

	/** Viewport preview only; no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimRTS|Obstruction")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
