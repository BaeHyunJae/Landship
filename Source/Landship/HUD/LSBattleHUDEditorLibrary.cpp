#include "HUD/LSBattleHUDEditorLibrary.h"

#if WITH_EDITOR
#include "HUD/LSBattleHUDWidget.h"
#include "HUD/LSCommandButton.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Styling/CoreStyle.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Engine/Font.h"

namespace
{
void Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size)
{
	auto* Slot = Canvas->AddChildToCanvas(Widget);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
}
UTextBlock* Label(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* Value, int32 Size = 14)
{
	auto* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetText(FText::FromString(Value));
	// 임시 Slate 합성 폰트는 WBP 저장 시 보존되지 않는다. 직렬화되는 폰트 에셋을 참조한다.
	UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	Text->SetFont(FSlateFontInfo(Font, Size, TEXT("Regular")));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 1)));
	Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Text;
}
UCanvasPanel* Panel(UWidgetTree* Tree, UCanvasPanel* Root, const TCHAR* Name, FVector2D Size)
{
	auto* Scale = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), Name);
	Scale->SetStretch(EStretch::ScaleToFit);
	auto* Box = Tree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(Size.X);
	Box->SetHeightOverride(Size.Y);
	auto* Border = Tree->ConstructWidget<UBorder>();
	Border->SetPadding(FMargin(0));
	Border->SetBrushColor(FLinearColor(0.025f, 0.045f, 0.075f, 0.96f));
	auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
	Border->SetContent(Canvas);
	Box->SetContent(Border);
	Scale->SetContent(Box);
	Place(Root, Scale, FVector2D::ZeroVector, Size);
	return Canvas;
}
} // namespace
#endif

bool ULSBattleHUDEditorLibrary::CreateBattleHUDAsset()
{
#if WITH_EDITOR
	const TCHAR* PackageName = TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD");
	if (LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD.WBP_BattleHUD")))
		return true;
	auto* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ULSBattleHUDWidget::StaticClass();
	auto* BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),
	                                                            CreatePackage(PackageName), TEXT("WBP_BattleHUD"),
	                                                            RF_Public | RF_Standalone, nullptr, GWarn));
	if (!BP)
		return false;
	UWidgetTree* Tree = BP->WidgetTree;
	if (!Tree)
		return false;
	auto* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Tree->RootWidget = Root;
	auto* Info = Panel(Tree, Root, TEXT("InfoPanel"), FVector2D(340, 125));
	Place(Info, Label(Tree, TEXT("SelectionText"), TEXT("대상을 선택하세요")), FVector2D(12, 8), FVector2D(316, 78));
	auto* Failure = Label(Tree, TEXT("FailureText"), TEXT(""), 11);
	Failure->SetColorAndOpacity(FSlateColor(FLinearColor(1, 0.75f, 0.2f)));
	Place(Info, Failure, FVector2D(12, 93), FVector2D(316, 25));
	auto* Commands = Panel(Tree, Root, TEXT("CommandPanel"), FVector2D(420, 336));
	Place(Commands, Label(Tree, TEXT("CommandGroupText"), TEXT("명령"), 13), FVector2D(12, 8), FVector2D(396, 25));
	Place(Commands, Label(Tree, TEXT("PopulationText"), TEXT(""), 11), FVector2D(12, 284), FVector2D(396, 20));
	Place(Commands, Label(Tree, TEXT("CommandHint"), TEXT(""), 10), FVector2D(12, 309), FVector2D(396, 22));
	for (int32 Index = 0; Index < 15; ++Index)
	{
		auto* Button = Tree->ConstructWidget<ULSCommandButton>(ULSCommandButton::StaticClass(),
		                                                       FName(*FString::Printf(TEXT("Command%d"), Index)));
		if (auto* Property = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
			Property->SetPropertyValue_InContainer(Button, false);
		auto* Lines = Tree->ConstructWidget<UVerticalBox>();
		const TCHAR* Prefixes[] = {TEXT("CommandKey"), TEXT("CommandTitle"), TEXT("CommandLevel"),
		                           TEXT("CommandState")};
		const int32 Sizes[] = {8, 10, 8, 8};
		for (int32 LineIndex = 0; LineIndex < 4; ++LineIndex)
		{
			auto* Line =
			    Label(Tree, *FString::Printf(TEXT("%s%d"), Prefixes[LineIndex], Index), TEXT(""), Sizes[LineIndex]);
			Line->SetJustification(ETextJustify::Center);
			Lines->AddChildToVerticalBox(Line)->SetHorizontalAlignment(HAlign_Center);
		}
		Button->SetContent(Lines);
		const FVector2D Position(12 + Index % 5 * 80, 36 + Index / 5 * 82);
		Place(Commands, Button, Position, FVector2D(76, 78));
		auto* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),
		                                                FName(*FString::Printf(TEXT("CommandProgress%d"), Index)));
		Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
		Bar->SetFillColorAndOpacity(FLinearColor(0.2f, 0.7f, 0.9f));
		Place(Commands, Bar, Position + FVector2D(0, 74), FVector2D(76, 4));
	}

	auto* Caption = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DiscoveryPanel"));
	Caption->SetPadding(FMargin(12));
	Caption->SetBrushColor(FLinearColor(0.08f, 0.14f, 0.22f, 0.95f));
	Caption->SetContent(Label(Tree, TEXT("DiscoveryText"), TEXT("적을 발견했습니다"), 18));
	Caption->SetVisibility(ESlateVisibility::Collapsed);
	auto* CaptionSlot = Root->AddChildToCanvas(Caption);
	CaptionSlot->SetAnchors(FAnchors(0.5f, 0));
	CaptionSlot->SetAlignment(FVector2D(0.5f, 0));
	CaptionSlot->SetPosition(FVector2D(0, 40));
	CaptionSlot->SetSize(FVector2D(300, 60));
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error)
		return false;
	FAssetRegistryModule::AssetCreated(BP);
	BP->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(
	    BP->GetOutermost(), BP,
	    *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Args);
#else
	return false;
#endif
}
