#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimRTSPathVisualizer.generated.h"

class ASimRTSGameMode;

/** Editor/debug overlay: dashed arrows for each unit's active and queued move segments. */
UCLASS()
class SIMRTS_API ASimRTSPathVisualizer : public AActor
{
	GENERATED_BODY()

public:
	ASimRTSPathVisualizer();

	virtual void Tick(float DeltaSeconds) override;

protected:
	void DrawUnitPaths(const ASimRTSGameMode& GameMode) const;
};
