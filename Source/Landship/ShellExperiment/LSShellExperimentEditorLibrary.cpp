#include "ShellExperiment/LSShellExperimentEditorLibrary.h"

#if WITH_EDITOR
#include "ShellExperiment/LSShellExperimentWidget.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "AI/NavigationSystemConfig.h"

namespace
{
	bool RegisterMissingWidgetGuids(UWidgetBlueprint* Blueprint)
	{
		bool bRegistered = false;
		// 컴파일러는 변수로 노출하지 않은 버튼 자식·루트에도 GUID를 요구한다.
		Blueprint->ForEachSourceWidget([Blueprint, &bRegistered](UWidget* Widget)
		{
			if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
			{
				Blueprint->OnVariableAdded(Widget->GetFName());
				bRegistered = true;
			}
		});
		return bRegistered;
	}

	bool HasRequiredControls(UWidgetBlueprint* Blueprint)
	{
		UWidgetTree* Tree = Blueprint->WidgetTree;
		if (!Tree || !Cast<UCanvasPanel>(Tree->RootWidget)) return false;
		for (const TCHAR* Name : {TEXT("HealthText"), TEXT("CoreText"), TEXT("LoadoutText"),
			TEXT("StatusText"), TEXT("HelpText")})
		{
			if (!Cast<UTextBlock>(Tree->FindWidget(FName(Name)))) return false;
		}
		if (!Cast<UProgressBar>(Tree->FindWidget(TEXT("ReloadProgress")))) return false;
		for (const TCHAR* Name : {TEXT("RestartButton"), TEXT("SwapButton")})
		{
			const UButton* Button = Cast<UButton>(Tree->FindWidget(FName(Name)));
			if (!Button || Button->GetIsFocusable()) return false;
		}
		return Tree->RootWidget->GetVisibility() == ESlateVisibility::SelfHitTestInvisible;
	}

	void Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size,
		FVector2D Anchor = FVector2D::ZeroVector, FVector2D Alignment = FVector2D::ZeroVector)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	UTextBlock* AddText(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, const TCHAR* Text,
		FVector2D Position, FVector2D Size, FVector2D Anchor = FVector2D::ZeroVector,
		FVector2D Alignment = FVector2D::ZeroVector)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Block->bIsVariable = true;
		Block->SetText(FText::FromString(Text));
		Block->SetFontSize(22);
		Block->SetColorAndOpacity(FSlateColor(FLinearColor(1, 0.95f, 0.82f, 1)));
		Block->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));
		Block->SetShadowOffset(FVector2D(2, 2));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Block, Position, Size, Anchor, Alignment);
		return Block;
	}

	void AddButton(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, const TCHAR* Label, float X,
		float Y = -65, FVector2D Anchor = FVector2D(1, 1))
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->bIsVariable = true;
		// 공개 setter가 없어 Slate 생성 전 편집 기본값을 직렬화한다. InitIsFocusable는 protected다.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Button->IsFocusable = false;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Button->SetVisibility(ESlateVisibility::Visible);
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Label"))));
		Text->SetText(FText::FromString(Label));
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Button->AddChild(Text);
		Place(Canvas, Button, FVector2D(X, Y), FVector2D(175, 42), Anchor);
	}
}
#endif

FString ULSShellExperimentEditorLibrary::GetConfiguredNavigationClass(UObject* WorldContext)
{
#if WITH_EDITOR
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UNavigationSystemConfig* Config = World ? World->GetWorldSettings()->GetNavigationSystemConfig() : nullptr;
	return Config ? Config->NavigationSystemClass.ToString() : FString();
#else
	return FString();
#endif
}

UObject* ULSShellExperimentEditorLibrary::BuildWidgetAsset()
{
#if WITH_EDITOR
	const TCHAR* PackageName = TEXT("/Game/Landship/ShellExperiment/UI/WBP_ShellExperiment");
	const TCHAR* ObjectPath = TEXT("/Game/Landship/ShellExperiment/UI/WBP_ShellExperiment.WBP_ShellExperiment");
	UObject* Existing = LoadObject<UObject>(nullptr, ObjectPath);
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Existing);
	if (Existing && !Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("Shell UI path contains a different asset type"));
		return nullptr;
	}
	if (!Blueprint)
	{
		UPackage* Package = CreatePackage(PackageName);
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = ULSShellExperimentWidget::StaticClass();
		Blueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),
			Package, TEXT("WBP_ShellExperiment"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!Blueprint) return nullptr;
		FAssetRegistryModule::AssetCreated(Blueprint);
	}
	if (Blueprint->ParentClass != ULSShellExperimentWidget::StaticClass())
	{
		UE_LOG(LogTemp, Error, TEXT("Shell UI must derive directly from ULSShellExperimentWidget"));
		return nullptr;
	}
	UWidgetTree* Tree = Blueprint->WidgetTree;
	if (!Tree)
	{
		Tree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
		Blueprint->WidgetTree = Tree;
	}
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(Tree->RootWidget);
	if (Tree->RootWidget && (!Canvas || Canvas->GetChildrenCount() > 0))
	{
		if (!HasRequiredControls(Blueprint))
		{
			UE_LOG(LogTemp, Error, TEXT("Existing Shell UI layout is incomplete or invalid; preserving it"));
			return nullptr;
		}
		if (RegisterMissingWidgetGuids(Blueprint))
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass) return nullptr;
			Blueprint->MarkPackageDirty();
		}
		return Blueprint;
	}
	if (!Canvas)
	{
		Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ShellCanvas"));
		Tree->RootWidget = Canvas;
	}
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	AddText(Tree, Canvas, TEXT("HealthText"), TEXT("요새 HP"), FVector2D(32, 24), FVector2D(400, 44));
	AddText(Tree, Canvas, TEXT("CoreText"), TEXT("중앙 동력로 HP"), FVector2D(-32, 24), FVector2D(400, 44),
		FVector2D(1, 0), FVector2D(1, 0));
	AddText(Tree, Canvas, TEXT("LoadoutText"), TEXT("중력 → 폭발"), FVector2D(0, -225), FVector2D(620, 44),
		FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddText(Tree, Canvas, TEXT("StatusText"), TEXT("포격 준비"), FVector2D(0, -165), FVector2D(620, 44),
		FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddText(Tree, Canvas, TEXT("ReactionText"), TEXT(""), FVector2D(0, -300), FVector2D(1000, 60),
		FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddText(Tree, Canvas, TEXT("HelpText"), TEXT("좌클릭: 발사 · F 누르기: 조합"),
		FVector2D(32, -100), FVector2D(1000, 100), FVector2D(0, 1));
	UProgressBar* Progress = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ReloadProgress"));
	Progress->bIsVariable = true;
	Progress->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Canvas, Progress, FVector2D(0, -180), FVector2D(620, 12), FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddButton(Tree, Canvas, TEXT("RestartButton"), TEXT("다시 시작"), -190);
	AddButton(Tree, Canvas, TEXT("SwapButton"), TEXT("순서 바꾸기"), -390);
	RegisterMissingWidgetGuids(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass || !HasRequiredControls(Blueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("Shell UI compilation or required control validation failed"));
		return nullptr;
	}
	Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>()->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Blueprint->MarkPackageDirty();
	return Blueprint;
#else
	return nullptr;
#endif
}

UObject* ULSShellExperimentEditorLibrary::UpgradeFeedbackWidgetAsset()
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(BuildWidgetAsset());
	if (!Blueprint) return nullptr;
	UWidgetTree* Tree = Blueprint->WidgetTree;
	UWidget* ExistingReaction = Tree->FindWidget(TEXT("ReactionText"));
	if (ExistingReaction && !Cast<UTextBlock>(ExistingReaction))
	{
		UE_LOG(LogTemp, Error, TEXT("Existing ReactionText is not a TextBlock; preserving the layout"));
		return nullptr;
	}
	Blueprint->Modify();
	Tree->Modify();
	if (!ExistingReaction)
	{
		AddText(Tree, CastChecked<UCanvasPanel>(Tree->RootWidget), TEXT("ReactionText"), TEXT(""),
			FVector2D(0, -300), FVector2D(1000, 60), FVector2D(0.5, 1), FVector2D(0.5, 0));
	}
	TArray<UWidget*> Widgets;
	Tree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			Text->Modify();
			Text->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));
			Text->SetShadowOffset(FVector2D(2, 2));
		}
	}
	RegisterMissingWidgetGuids(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass || !HasRequiredControls(Blueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("Shell feedback UI compilation failed"));
		return nullptr;
	}
	Blueprint->MarkPackageDirty();
	return Blueprint;
#else
	return nullptr;
#endif
}

UObject* ULSShellExperimentEditorLibrary::UpgradeP1WidgetAsset()
{
#if WITH_EDITOR
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(BuildWidgetAsset());
	if (!Blueprint) return nullptr;
	UWidgetTree* Tree = Blueprint->WidgetTree;
	const TCHAR* ButtonNames[] = {TEXT("Slot1GravityButton"), TEXT("Slot1BlastButton"), TEXT("Slot1SplitButton"),
		TEXT("Slot2GravityButton"), TEXT("Slot2BlastButton"), TEXT("Slot2SplitButton")};
	const TCHAR* Labels[] = {TEXT("슬롯 1: 중력"), TEXT("슬롯 1: 폭발"), TEXT("슬롯 1: 분열"),
		TEXT("슬롯 2: 중력"), TEXT("슬롯 2: 폭발"), TEXT("슬롯 2: 분열")};
	bool bNeedsUpgrade = false;
	for (const TCHAR* Name : ButtonNames)
	{
		if (UWidget* Existing = Tree->FindWidget(FName(Name)))
		{
			const UButton* Button = Cast<UButton>(Existing);
			if (!Button || Button->GetIsFocusable())
			{
				UE_LOG(LogTemp, Error, TEXT("Existing P1 control %s is invalid; preserving the layout"), Name);
				return nullptr;
			}
		}
		else bNeedsUpgrade = true;
	}
	UWidget* ExistingStatus = Tree->FindWidget(TEXT("SplitStatusText"));
	if (ExistingStatus && !Cast<UTextBlock>(ExistingStatus))
	{
		UE_LOG(LogTemp, Error, TEXT("Existing SplitStatusText is not a TextBlock; preserving the layout"));
		return nullptr;
	}
	bNeedsUpgrade |= !ExistingStatus;
	if (!bNeedsUpgrade) return Blueprint;
	Blueprint->Modify();
	Tree->Modify();
	UCanvasPanel* Canvas = CastChecked<UCanvasPanel>(Tree->RootWidget);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ButtonNames); ++Index)
	{
		if (!Tree->FindWidget(FName(ButtonNames[Index])))
		{
			AddButton(Tree, Canvas, FName(ButtonNames[Index]), Labels[Index],
				-272 + (Index % 3) * 184, Index < 3 ? -455 : -405, FVector2D(0.5, 1));
		}
	}
	if (!ExistingStatus)
	{
		AddText(Tree, Canvas, TEXT("SplitStatusText"), TEXT("분열 잠김"), FVector2D(0, -350),
			FVector2D(700, 44), FVector2D(0.5, 1), FVector2D(0.5, 0));
	}
	RegisterMissingWidgetGuids(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass || !HasRequiredControls(Blueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("Shell P1 UI compilation failed"));
		return nullptr;
	}
	Blueprint->MarkPackageDirty();
	return Blueprint;
#else
	return nullptr;
#endif
}
