#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OrderManager.generated.h"

class APlayerController;
class USelectionManager;

/**
 * Issues sim orders from Unreal input.
 * Right-click with a selection → move order (world hit → discrete grid).
 * Shift+right-click → is_next waypoint (runs after the current move).
 */
UCLASS()
class SIMRTS_API UOrderManager : public UObject
{
	GENERATED_BODY()

public:
	void HandleRightClick(APlayerController* PlayerController, const USelectionManager* SelectionManager);
};
