#include "SimRTSStartScreen.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

USimRTSStartScreen::USimRTSStartScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

TSharedRef<SWidget> USimRTSStartScreen::RebuildWidget()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		BuildTreeIfNeeded();
	}
	return Super::RebuildWidget();
}

void USimRTSStartScreen::BuildTreeIfNeeded()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	UBorder* Dimmer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dimmer"));
	Dimmer->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f));
	Dimmer->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* DimmerSlot = Root->AddChildToCanvas(Dimmer))
	{
		DimmerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimmerSlot->SetOffsets(FMargin(0.f));
	}

	StartButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("StartButton"));
	StartButton->SetBackgroundColor(FLinearColor(0.16f, 0.52f, 0.32f));
	StartButton->OnClicked.AddDynamic(this, &USimRTSStartScreen::HandleStartClicked);

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StartLabel"));
	Label->SetText(FText::FromString(TEXT("Start")));
	Label->SetJustification(ETextJustify::Center);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 24));
	StartButton->AddChild(Label);
	if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Label->Slot))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	if (UCanvasPanelSlot* ButtonSlot = Root->AddChildToCanvas(StartButton))
	{
		ButtonSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ButtonSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ButtonSlot->SetSize(FVector2D(240.f, 64.f));
		ButtonSlot->SetPosition(FVector2D::ZeroVector);
	}
}

void USimRTSStartScreen::HandleStartClicked()
{
	OnStartClicked.Broadcast();
}
