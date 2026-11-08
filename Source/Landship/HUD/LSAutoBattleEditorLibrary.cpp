#include "HUD/LSAutoBattleEditorLibrary.h"
#if WITH_EDITOR
#include "HUD/LSAutoBattleWidget.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/Font.h"
#include "UObject/UnrealType.h"
#endif

bool ULSAutoBattleEditorLibrary::CreateAutoBattleWidget()
{
#if WITH_EDITOR
	const TCHAR* PackageName = TEXT("/Game/Developers/HMH/TABS/WBP_AutoBattle");
	if (LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Developers/HMH/TABS/WBP_AutoBattle.WBP_AutoBattle")))
	{
		return true;
	}
	auto* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ULSAutoBattleWidget::StaticClass();
	auto* BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),
	                                                            CreatePackage(PackageName), TEXT("WBP_AutoBattle"),
	                                                            RF_Public | RF_Standalone, nullptr, GWarn));
	if (!BP || !BP->WidgetTree)
	{
		return false;
	}
	UWidgetTree* Tree = BP->WidgetTree;
	auto* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Tree->RootWidget = Root;
	auto* Scale = Tree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	auto* ScaleSlot = Root->AddChildToCanvas(Scale);
	ScaleSlot->SetAnchors(FAnchors(0, 0, 0, 1));
	ScaleSlot->SetOffsets(FMargin(18, 18, 336, 18));
	auto* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Border->SetBrushColor(FLinearColor(0.025f, 0.04f, 0.065f, 0.96f));
	Border->SetPadding(FMargin(0));
	auto* Size = Tree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(336);
	Size->SetHeightOverride(840);
	Size->SetContent(Border);
	Scale->SetContent(Size);
	auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
	Border->SetContent(Canvas);
	const auto Place = [Canvas](UWidget* Widget, FVector2D Position, FVector2D Size)
	{
		auto* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	};
	const auto Label = [Tree](const FName Name, const FString& Value, int32 Size)
	{
		auto* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size,
		                             TEXT("Regular")));
		Text->SetText(FText::FromString(Value));
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.96f, 1)));
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Text->SetAutoWrapText(true);
		return Text;
	};
	Place(Label(TEXT("Title"), TEXT("LANDSHIP / AUTO BATTLE"), 16), FVector2D(18, 16), FVector2D(300, 28));
	Place(Label(TEXT("Phase"), TEXT("전장 준비 중"), 24), FVector2D(18, 48), FVector2D(300, 40));
	Place(Label(TEXT("Economy"), TEXT(""), 16), FVector2D(18, 96), FVector2D(300, 28));
	Place(Label(TEXT("Health"), TEXT(""), 15), FVector2D(18, 132), FVector2D(300, 50));
	Place(Label(TEXT("UnitsTitle"), TEXT("1. 병력 편성"), 15), FVector2D(18, 202), FVector2D(300, 28));
	const auto Button = [&](const TCHAR* Name, const TCHAR* Value, FVector2D Position, FVector2D Size)
	{
		auto* B = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		if (auto* Focusable = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
		{
			Focusable->SetPropertyValue_InContainer(B, false);
		}
		B->SetBackgroundColor(FLinearColor(0.15f, 0.22f, 0.3f));
		auto* Text = Label(FName(*FString::Printf(TEXT("%sLabel"), Name)), Value, 13);
		Text->SetAutoWrapText(false);
		Text->SetJustification(ETextJustify::Center);
		B->SetContent(Text);
		Place(B, Position, Size);
	};
	Button(TEXT("Spear"), TEXT("창병"), FVector2D(18, 238), FVector2D(72, 42));
	Button(TEXT("Archer"), TEXT("궁병"), FVector2D(94, 238), FVector2D(72, 42));
	Button(TEXT("Cavalry"), TEXT("기마병"), FVector2D(170, 238), FVector2D(72, 42));
	Button(TEXT("Medic"), TEXT("의무병"), FVector2D(246, 238), FVector2D(72, 42));
	Button(TEXT("Castle"), TEXT("성 위치 배치"), FVector2D(18, 288), FVector2D(300, 38));
	Place(Label(TEXT("WeaponTitle"), TEXT("2. 성 자동 무장"), 15), FVector2D(18, 348), FVector2D(300, 28));
	Button(TEXT("Cannon"), TEXT("일반 대포"), FVector2D(18, 386), FVector2D(96, 42));
	Button(TEXT("Shotgun"), TEXT("산탄포"), FVector2D(120, 386), FVector2D(96, 42));
	Button(TEXT("Ballista"), TEXT("발리스타"), FVector2D(222, 386), FVector2D(96, 42));
	Place(Label(TEXT("Weapon"), TEXT(""), 13), FVector2D(18, 440), FVector2D(300, 38));
	Button(TEXT("Remove"), TEXT("선택 제거"), FVector2D(18, 490), FVector2D(144, 38));
	Button(TEXT("Reset"), TEXT("편성 초기화"), FVector2D(174, 490), FVector2D(144, 38));
	Button(TEXT("Start"), TEXT("전투 시작  [Enter]"), FVector2D(18, 546), FVector2D(300, 48));
	Button(TEXT("Retry"), TEXT("편성 복원"), FVector2D(18, 604), FVector2D(300, 44));
	Place(Label(TEXT("Message"), TEXT(""), 14), FVector2D(18, 666), FVector2D(300, 72));
	Place(Label(TEXT("Help"),
	            TEXT("우클릭 유지: 회전 + WASD / Q·E 상하\n가운데 드래그: 화면 이동\n휠: 앞뒤 · 우클릭+휠: 속도\nShift "
	                 "가속 · Home 복귀"),
	            13),
	      FVector2D(18, 752), FVector2D(300, 72));
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error)
	{
		return false;
	}
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

bool ULSAutoBattleEditorLibrary::UpgradeAutoBattleWidget()
{
#if WITH_EDITOR
	auto* BP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Developers/HMH/TABS/WBP_AutoBattle.WBP_AutoBattle"));
	if (!BP || !BP->WidgetTree)
	{
		return false;
	}
	UWidgetTree* Tree = BP->WidgetTree;
	auto FixEditorStyle = [BP, Tree]()
	{
		if (!Tree->FindWidget(TEXT("ForceComparisonPanel")))
		{
			auto* Root = Cast<UCanvasPanel>(Tree->RootWidget);
			if (!Root)
			{
				return false;
			}
			auto* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ForceComparisonPanel"));
			Panel->SetBrushColor(FLinearColor(0.025f, 0.04f, 0.065f, 0.96f));
			Panel->SetPadding(FMargin(0));
			auto* Slot = Root->AddChildToCanvas(Panel);
			Slot->SetAnchors(FAnchors(0.5f, 0));
			Slot->SetAlignment(FVector2D(0.5f, 0));
			Slot->SetPosition(FVector2D(0, 18));
			Slot->SetSize(FVector2D(460, 144));
			auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
			Panel->SetContent(Canvas);
			auto Text = [Tree, Canvas](const TCHAR* Name, const TCHAR* Value, float X, float Y, float Width,
			                           int32 FontSize, FLinearColor Color)
			{
				auto* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
				T->SetText(FText::FromString(Value));
				T->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")),
				                          FontSize, TEXT("Regular")));
				T->SetColorAndOpacity(FSlateColor(Color));
				T->SetVisibility(ESlateVisibility::HitTestInvisible);
				auto* Cell = Canvas->AddChildToCanvas(T);
				Cell->SetPosition(FVector2D(X, Y));
				Cell->SetSize(FVector2D(Width, 30));
			};
			Text(TEXT("ForceComparisonTitle"), TEXT("편성 비교 · 배치 기준 / 성 제외"), 18, 10, 424, 15,
			     FLinearColor::White);
			Text(TEXT("ForcePopulationHeader"), TEXT("인구수"), 136, 42, 110, 13, FLinearColor(0.75f, 0.8f, 0.85f));
			Text(TEXT("ForceCostHeader"), TEXT("코스트 합"), 294, 42, 148, 13, FLinearColor(0.75f, 0.8f, 0.85f));
			const FLinearColor Ally(0.3f, 0.8f, 1), Enemy(1, 0.45f, 0.32f);
			Text(TEXT("AllyForceTitle"), TEXT("아군"), 18, 69, 100, 19, Ally);
			Text(TEXT("AllyForcePopulation"), TEXT("0"), 136, 69, 138, 19, Ally);
			Text(TEXT("AllyForceCost"), TEXT("0"), 294, 69, 148, 19, Ally);
			Text(TEXT("EnemyForceTitle"), TEXT("적군"), 18, 104, 100, 19, Enemy);
			Text(TEXT("EnemyForcePopulation"), TEXT("0"), 136, 104, 138, 19, Enemy);
			Text(TEXT("EnemyForceCost"), TEXT("0"), 294, 104, 148, 19, Enemy);
		}
		BP->ForEachSourceWidget(
		    [BP](UWidget* W)
		    {
			    if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))
			    {
				    BP->OnVariableAdded(W->GetFName());
			    }
		    });

		for (const TCHAR* Button : {TEXT("EditAllies"), TEXT("EditEnemies"), TEXT("SaveNewPreset"),
		                            TEXT("OverwritePreset"), TEXT("LoadSelectedPreset"), TEXT("ReloadPresetList")})
		{
			if (auto* Text = Cast<UTextBlock>(Tree->FindWidget(FName(*FString::Printf(TEXT("%sLabel"), Button)))))
			{
				Text->SetAutoWrapText(false);
			}
		}
		if (auto* Input = Cast<UEditableTextBox>(Tree->FindWidget(TEXT("PresetName"))))
		{
			auto Style = Input->GetWidgetStyle();
			Style.SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 14,
			                             TEXT("Regular")));
			Style.ForegroundColor = FSlateColor(FLinearColor(0.04f, 0.06f, 0.08f));
			Input->SetWidgetStyle(Style);
			Input->SetForegroundColor(FLinearColor(0.04f, 0.06f, 0.08f));
			Input->SetHintText(FText::FromString(TEXT("프리셋 이름")));
		}
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error)
		{
			return false;
		}
		BP->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(BP->GetOutermost(), BP,
		                             *FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),
		                                                                      FPackageName::GetAssetPackageExtension()),
		                             Args);
	};
	if (Tree->FindWidget(TEXT("EditorPanel")))
	{
		return FixEditorStyle();
	}
	auto* Root = Cast<UCanvasPanel>(Tree->RootWidget);
	if (!Root)
	{
		return false;
	}
	auto* Scale = Tree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	auto* RootSlot = Root->AddChildToCanvas(Scale);
	RootSlot->SetAnchors(FAnchors(1, 0, 1, 1));
	RootSlot->SetOffsets(FMargin(-354, 18, 336, 18));
	auto* Size = Tree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(336);
	Size->SetHeightOverride(840);
	Scale->SetContent(Size);
	auto* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EditorPanel"));
	Panel->SetBrushColor(FLinearColor(0.025f, 0.04f, 0.065f, 0.96f));
	Panel->SetPadding(FMargin(0));
	Size->SetContent(Panel);
	auto* Canvas = Tree->ConstructWidget<UCanvasPanel>();
	Panel->SetContent(Canvas);
	auto Place = [Canvas](UWidget* W, float Y, float H, float X = 18, float Width = 300)
	{
		auto* Slot = Canvas->AddChildToCanvas(W);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(Width, H));
	};
	auto Label = [Tree](const TCHAR* Name, const TCHAR* Text, int32 FontSize = 15)
	{
		auto* W = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		W->SetText(FText::FromString(Text));
		W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), FontSize,
		                          TEXT("Regular")));
		W->SetAutoWrapText(true);
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
		return W;
	};
	auto Button = [&](const TCHAR* Name, const TCHAR* Text, float Y, float X = 18, float Width = 300)
	{
		auto* B = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		if (auto* Focusable = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
		{
			Focusable->SetPropertyValue_InContainer(B, false);
		}
		auto* T = Label(*FString::Printf(TEXT("%sLabel"), Name), Text, 14);
		T->SetJustification(ETextJustify::Center);
		B->SetContent(T);
		B->SetBackgroundColor(FLinearColor(0.15f, 0.22f, 0.3f));
		Place(B, Y, 36, X, Width);
	};
	Place(Label(TEXT("EditorTitle"), TEXT("시나리오 편집"), 23), 16, 36);
	Button(TEXT("EditAllies"), TEXT("아군 배치"), 66, 18, 144);
	Button(TEXT("EditEnemies"), TEXT("적 배치"), 66, 174, 144);
	Place(Label(TEXT("TeamStatus"), TEXT("")), 112, 30);
	Place(Label(TEXT("EditorHelp"),
	            TEXT("병종 선택 → 좌클릭 드래그 생성\nDelete → 드래그 삭제\nShift+클릭 선택 → 지면 클릭 이동\n아군은 "
	                 "코스트·인구 상한 없이 배치"),
	            14),
	      152, 100);
	Place(Label(TEXT("PresetTitle"), TEXT("프리셋 저장 · 불러오기"), 18), 264, 30);
	auto* Name = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("PresetName"));
	Name->SetHintText(FText::FromString(TEXT("새 이름: 창병 전열 테스트")));
	Place(Name, 306, 38);
	Button(TEXT("SaveNewPreset"), TEXT("새 이름 저장"), 354, 18, 144);
	Button(TEXT("OverwritePreset"), TEXT("이름 덮어쓰기"), 354, 174, 144);
	auto* List = Tree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("PresetList"));
	Place(List, 404, 38);
	Button(TEXT("LoadSelectedPreset"), TEXT("선택 프리셋 불러오기"), 452, 18, 204);
	Button(TEXT("ReloadPresetList"), TEXT("새로고침"), 452, 230, 88);
	Place(Label(TEXT("EditorMessage"), TEXT(""), 14), 506, 74);
	Place(Label(TEXT("PresetFolder"), TEXT("파일: Saved/AutoBattlePresets\nJSON을 복사해 팀원과 공유"), 12), 588, 48);
	// 기존 WBP에 새 위젯을 추가할 때는 에디터의 변수 GUID 등록도 함께 수행한다.
	BP->ForEachSourceWidget(
	    [BP](UWidget* W)
	    {
		    if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))
		    {
			    BP->OnVariableAdded(W->GetFName());
		    }
	    });
	return FixEditorStyle();
#else
	return false;
#endif
}
