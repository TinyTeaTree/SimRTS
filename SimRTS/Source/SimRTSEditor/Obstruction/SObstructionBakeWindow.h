#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

class SObstructionBakeWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SObstructionBakeWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FString GetDefaultLevelJsonPath() const;
	FString GetDefaultSpawnsJsonPath() const;
	float GetDefaultGridScale() const;

	FReply OnBrowseLevelClicked();
	FReply OnBrowseSpawnsClicked();
	FReply OnBakeClicked();

	TSharedPtr<SEditableTextBox> LevelPathTextBox;
	TSharedPtr<SEditableTextBox> SpawnsPathTextBox;
	TSharedPtr<SEditableTextBox> GridScaleTextBox;
	TSharedPtr<STextBlock> StatusText;
};
