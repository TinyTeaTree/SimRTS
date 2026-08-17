#include "SObstructionBakeWindow.h"

#include "ObstructionBake.h"
#include "SimRTSGameMode.h"
#include "SpawnBake.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimRTSObstructionBake"

FString SObstructionBakeWindow::GetDefaultLevelJsonPath() const
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/Levels/DefaultLevel.json"));
}

FString SObstructionBakeWindow::GetDefaultSpawnsJsonPath() const
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("Data/Levels/DefaultSpawns.json"));
}

float SObstructionBakeWindow::GetDefaultGridScale() const
{
	if (const ASimRTSGameMode* CDO = GetDefault<ASimRTSGameMode>())
	{
		return CDO->GetGridScale();
	}
	return 10.f;
}

void SObstructionBakeWindow::Construct(const FArguments& InArgs)
{
	const FString DefaultLevelPath = GetDefaultLevelJsonPath();
	const FString DefaultSpawnsPath = GetDefaultSpawnsJsonPath();
	const FString DefaultGridScale = FString::SanitizeFloat(GetDefaultGridScale());

	ChildSlot
	[
		SNew(SBox)
		.Padding(12.f)
		.MinDesiredWidth(520.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"Title",
					"Bake obstruction volumes and spawn markers into level JSON files."))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LevelJsonLabel", "Level JSON (obstruction)"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(LevelPathTextBox, SEditableTextBox)
					.Text(FText::FromString(DefaultLevelPath))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseLevel", "Browse..."))
					.OnClicked(this, &SObstructionBakeWindow::OnBrowseLevelClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SpawnsJsonLabel", "Spawns JSON"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(SpawnsPathTextBox, SEditableTextBox)
					.Text(FText::FromString(DefaultSpawnsPath))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseSpawns", "Browse..."))
					.OnClicked(this, &SObstructionBakeWindow::OnBrowseSpawnsClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GridScaleLabel", "Grid Scale (UU per cell)"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 12.f)
			[
				SAssignNew(GridScaleTextBox, SEditableTextBox)
				.Text(FText::FromString(DefaultGridScale))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Bake", "Bake Level"))
				.OnClicked(this, &SObstructionBakeWindow::OnBakeClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT(
					"Ready",
					"Ready. Place Obstruction Volume / Cylinder and Spawn Marker actors, then bake."))
			]
		]
	];
}

FReply SObstructionBakeWindow::OnBrowseLevelClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	TArray<FString> OutFiles;
	const FString DefaultPath = LevelPathTextBox.IsValid()
		? FPaths::GetPath(LevelPathTextBox->GetText().ToString())
		: FPaths::ProjectContentDir() / TEXT("Data/Levels");

	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Level JSON"),
		DefaultPath,
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (bOpened && OutFiles.Num() > 0 && LevelPathTextBox.IsValid())
	{
		LevelPathTextBox->SetText(FText::FromString(OutFiles[0]));
	}

	return FReply::Handled();
}

FReply SObstructionBakeWindow::OnBrowseSpawnsClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	TArray<FString> OutFiles;
	const FString DefaultPath = SpawnsPathTextBox.IsValid()
		? FPaths::GetPath(SpawnsPathTextBox->GetText().ToString())
		: FPaths::ProjectContentDir() / TEXT("Data/Levels");

	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Spawns JSON"),
		DefaultPath,
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (bOpened && OutFiles.Num() > 0 && SpawnsPathTextBox.IsValid())
	{
		SpawnsPathTextBox->SetText(FText::FromString(OutFiles[0]));
	}

	return FReply::Handled();
}

FReply SObstructionBakeWindow::OnBakeClicked()
{
	const FString LevelPath = LevelPathTextBox.IsValid()
		? LevelPathTextBox->GetText().ToString().TrimStartAndEnd()
		: FString();
	const FString SpawnsPath = SpawnsPathTextBox.IsValid()
		? SpawnsPathTextBox->GetText().ToString().TrimStartAndEnd()
		: FString();

	float GridScale = GetDefaultGridScale();
	if (GridScaleTextBox.IsValid())
	{
		LexFromString(GridScale, *GridScaleTextBox->GetText().ToString().TrimStartAndEnd());
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(LOCTEXT("Baking", "Baking..."));
	}

	FObstructionBakeParams ObstructionParams;
	ObstructionParams.JsonPath = LevelPath;
	ObstructionParams.GridScale = GridScale;
	const FObstructionBakeResult ObstructionResult = ObstructionBake::BakeToJsonFile(ObstructionParams);
	if (!ObstructionResult.bSuccess)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(
				LOCTEXT("ObstructionBakeFail", "Obstruction bake failed: {0}"),
				FText::FromString(ObstructionResult.Error)));
		}
		return FReply::Handled();
	}

	FSpawnBakeParams SpawnParams;
	SpawnParams.LevelJsonPath = LevelPath;
	SpawnParams.SpawnsJsonPath = SpawnsPath;
	SpawnParams.GridScale = GridScale;
	const FSpawnBakeResult SpawnResult = SpawnBake::BakeToJsonFile(SpawnParams);
	if (!SpawnResult.bSuccess)
	{
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::Format(
				LOCTEXT(
					"SpawnBakeFail",
					"Obstruction bake OK, but spawn bake failed: {0}\nLevel wrote: {1}"),
				FText::FromString(SpawnResult.Error),
				FText::FromString(LevelPath)));
		}
		return FReply::Handled();
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(
			LOCTEXT(
				"BakeOk",
				"Bake complete.\n"
				"Obstructions: Volumes={0}  Map={1}x{2}  Blocked={3}\n"
				"Spawns: Count={4}\n"
				"Wrote:\n{5}\n{6}"),
			FText::AsNumber(ObstructionResult.VolumeCount),
			FText::AsNumber(ObstructionResult.Width),
			FText::AsNumber(ObstructionResult.Height),
			FText::AsNumber(ObstructionResult.BlockedCells),
			FText::AsNumber(SpawnResult.SpawnCount),
			FText::FromString(LevelPath),
			FText::FromString(SpawnsPath)));
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
