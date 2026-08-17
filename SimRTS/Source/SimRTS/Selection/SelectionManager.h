#pragma once

#include "CoreMinimal.h"
#include "SelectionHolder.h"
#include "UObject/Object.h"
#include "SelectionManager.generated.h"

class APlayerController;

/**
 * Unreal-owned selection service: click traces + selection state.
 * Sole authority for changing selection; data lives in FSelectionHolder.
 */
UCLASS()
class SIMRTS_API USelectionManager : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Left-click: select unit (replace), or clear if miss.
	 * Shift+left-click unit: add to selection (empty ground keeps current selection).
	 */
	void HandleLeftClick(APlayerController* PlayerController);

	const FSelectionHolder& GetSelection() const { return Selection; }

private:
	void ApplySelectionVisuals(APlayerController* PlayerController, const TArray<int32>& PreviousUnitIds);

	FSelectionHolder Selection;
};
