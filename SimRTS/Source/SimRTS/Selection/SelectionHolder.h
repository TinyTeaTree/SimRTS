#pragma once

#include "CoreMinimal.h"

/** Plain (non-UObject) holder for currently selected sim unit ids. */
class SIMRTS_API FSelectionHolder
{
public:
	bool HasSelection() const { return SelectedUnitIds.Num() > 0; }

	const TArray<int32>& GetSelectedUnitIds() const { return SelectedUnitIds; }

	/** Replace selection with a single unit. */
	void Select(int32 UnitId)
	{
		SelectedUnitIds.Reset();
		SelectedUnitIds.Add(UnitId);
	}

	/** Add unit if not already selected (shift-click). */
	void Add(int32 UnitId)
	{
		SelectedUnitIds.AddUnique(UnitId);
	}

	void Clear()
	{
		SelectedUnitIds.Reset();
	}

	bool IsSelected(int32 UnitId) const
	{
		return SelectedUnitIds.Contains(UnitId);
	}

private:
	TArray<int32> SelectedUnitIds;
};
