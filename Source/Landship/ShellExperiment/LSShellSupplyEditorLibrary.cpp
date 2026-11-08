#include "LSShellSupplyEditorLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

bool ULSShellSupplyEditorLibrary::ConfigureFloorCollision(UPrimitiveComponent* Floor)
{
#if WITH_EDITOR
	if (!Floor || !Floor->GetWorld() || Floor->GetWorld()->IsGameWorld()) return false;
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetCollisionObjectType(LSCollisionChannels::Ground);
	Floor->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
	return Floor->GetCollisionObjectType() == LSCollisionChannels::Ground &&
		Floor->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion) == ECR_Ignore;
#else
	return false;
#endif
}

FString ULSShellSupplyEditorLibrary::GetFloorPhysicsError(UPrimitiveComponent* Floor, UPhysicalMaterial* ExpectedMaterial)
{
#if WITH_EDITOR
	if (!Floor || !Floor->GetWorld() || Floor->GetWorld()->IsGameWorld())
		return TEXT("Floor validation requires an editor-world primitive");
	if (Floor->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		return TEXT("Supply floor collision must be QueryAndPhysics");
	for (ECollisionChannel Channel : {LSCollisionChannels::Building, LSCollisionChannels::ObstacleProjectile, ECC_Visibility})
		if (Floor->GetCollisionResponseToChannel(Channel) != ECR_Block)
			return FString::Printf(TEXT("Supply floor channel %d must Block"), static_cast<int32>(Channel));
	if (Floor->GetCollisionObjectType() != LSCollisionChannels::Ground)
		return FString::Printf(TEXT("Supply floor object type must be Ground (%d), found %d"),
			static_cast<int32>(LSCollisionChannels::Ground), static_cast<int32>(Floor->GetCollisionObjectType()));
	if (Floor->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion) != ECR_Ignore)
		return FString::Printf(TEXT("Supply floor ExplosionOcclusion response must be Ignore (%d), found %d"),
			static_cast<int32>(ECR_Ignore),
			static_cast<int32>(Floor->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion)));
	if (!ExpectedMaterial) return TEXT("Expected supply floor physical material is missing");
	const UPhysicalMaterial* ActualMaterial = Floor->BodyInstance.GetPhysMaterialOverride();
	if (ActualMaterial != ExpectedMaterial)
		return FString::Printf(TEXT("Supply floor physical material override must reference %s, found %s"),
			*ExpectedMaterial->GetPathName(), *GetPathNameSafe(ActualMaterial));
	return FString();
#else
	return TEXT("Floor validation requires an editor build");
#endif
}

#if WITH_EDITOR
#include "LSShellSupplyExperimentWidget.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace
{
	const TCHAR* const FullButtonNames[] = {TEXT("ThrustButton"), TEXT("BlastButton"), TEXT("ElectricButton"), TEXT("GravityButton"),
		TEXT("IronButton"), TEXT("AdhesionButton"), TEXT("CoolingButton"), TEXT("SplitButton")};
	const TCHAR* const FullButtonLabels[] = {TEXT("➜ 추진"), TEXT("✹ 폭발"), TEXT("ϟ 전격"), TEXT("중력"),
		TEXT("철구"), TEXT("접착"), TEXT("냉각"), TEXT("분열")};

	void Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size,
		FVector2D Anchor = FVector2D::ZeroVector, FVector2D Alignment = FVector2D::ZeroVector)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
	}

	void AddBackground(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, FVector2D Position,
		FVector2D Size, FVector2D Anchor = FVector2D::ZeroVector, FVector2D Alignment = FVector2D::ZeroVector)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Border->SetBrushColor(FLinearColor(0.025f, 0.035f, 0.055f, 0.88f));
		Border->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Border, Position, Size, Anchor, Alignment);
	}

	UTextBlock* AddText(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, const TCHAR* Label,
		FVector2D Position, FVector2D Size, int32 FontSize = 18,
		FVector2D Anchor = FVector2D::ZeroVector, FVector2D Alignment = FVector2D::ZeroVector)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->bIsVariable = true;
		Text->SetText(FText::FromString(Label));
		Text->SetFontSize(FontSize);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 1)));
		Text->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.95f));
		Text->SetShadowOffset(FVector2D(1, 1));
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Text, Position, Size, Anchor, Alignment);
		return Text;
	}

	UButton* AddButton(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, const TCHAR* Label,
		FVector2D Position, FVector2D Size, FVector2D Anchor = FVector2D::ZeroVector,
		FVector2D Alignment = FVector2D::ZeroVector)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->bIsVariable = true;
		// Slate 생성 전에 키보드 포커스를 받지 않는 기본값을 직렬화한다.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Button->IsFocusable = false;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Label"))));
		Text->SetText(FText::FromString(Label));
		Text->SetFontSize(18);
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Button->AddChild(Text);
		Place(Canvas, Button, Position, Size, Anchor, Alignment);
		return Button;
	}

	void AddProgress(UWidgetTree* Tree, UCanvasPanel* Canvas, FName Name, FLinearColor Color,
		FVector2D Position, FVector2D Size, FVector2D Anchor = FVector2D::ZeroVector,
		FVector2D Alignment = FVector2D::ZeroVector)
	{
		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
		Bar->bIsVariable = true;
		Bar->SetPercent(1);
		Bar->SetFillColorAndOpacity(Color);
		Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Bar, Position, Size, Anchor, Alignment);
	}

	bool HasButton(UWidgetTree* Tree, FName Name)
	{
		const UButton* Button = Cast<UButton>(Tree->FindWidget(Name));
		return Button && !Button->GetIsFocusable() && Cast<UTextBlock>(Button->GetContent());
	}

	bool HasFullSelector(UWidgetTree* Tree)
	{
		for (const TCHAR* Name : FullButtonNames)
			if (!HasButton(Tree, FName(Name))) return false;
		return true;
	}

	bool CanAddCombinationDescription(UWidgetTree* Tree)
	{
		const UCanvasPanel* Panel = Cast<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel")));
		const UCanvasPanelSlot* PanelSlot = Panel ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
		const UWidget* Background = Tree->FindWidget(TEXT("CompositionBackground"));
		const UCanvasPanelSlot* BackgroundSlot = Background ? Cast<UCanvasPanelSlot>(Background->Slot) : nullptr;
		if (!PanelSlot || !BackgroundSlot || PanelSlot->GetAnchors().Minimum != PanelSlot->GetAnchors().Maximum ||
			BackgroundSlot->GetPosition() != FVector2D::ZeroVector || BackgroundSlot->GetAlignment() != FVector2D::ZeroVector ||
			BackgroundSlot->GetAnchors().Minimum != FVector2D::ZeroVector ||
			BackgroundSlot->GetAnchors().Maximum != FVector2D::ZeroVector || BackgroundSlot->GetSize() != PanelSlot->GetSize()) return false;
		for (const UWidget* Child : Panel->GetAllChildren())
		{
			const UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Child->Slot);
			if (!Slot || Slot->GetAnchors().Minimum != Slot->GetAnchors().Maximum) return false;
		}
		return true;
	}

	bool AddCombinationDescription(UWidgetTree* Tree)
	{
		if (Tree->FindWidget(TEXT("CombinationText"))) return true;
		if (!CanAddCombinationDescription(Tree))
		{
			UE_LOG(LogTemp, Error, TEXT("Supply composition layout cannot add a description while preserving control positions"));
			return false;
		}
		UCanvasPanel* Panel = CastChecked<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel")));
		UCanvasPanelSlot* PanelSlot = CastChecked<UCanvasPanelSlot>(Panel->Slot);
		UCanvasPanelSlot* BackgroundSlot = CastChecked<UCanvasPanelSlot>(Tree->FindWidget(TEXT("CompositionBackground"))->Slot);
		// 조합판을 위로만 늘리고 기존 컨트롤의 화면 위치와 F/R 위치를 보존한다.
		PanelSlot->Modify();
		PanelSlot->SetPosition(PanelSlot->GetPosition() - FVector2D(0, 48 * (1 - PanelSlot->GetAlignment().Y)));
		PanelSlot->SetSize(PanelSlot->GetSize() + FVector2D(0, 48));
		for (UWidget* Child : Panel->GetAllChildren())
		{
			UCanvasPanelSlot* Slot = CastChecked<UCanvasPanelSlot>(Child->Slot);
			Slot->Modify();
			if (Slot == BackgroundSlot) Slot->SetSize(Slot->GetSize() + FVector2D(0, 48));
			else Slot->SetPosition(Slot->GetPosition() + FVector2D(0, 48 * (1 - Slot->GetAnchors().Minimum.Y)));
		}
		AddText(Tree, Panel, TEXT("CombinationText"), TEXT(""), FVector2D(16, 8), FVector2D(488, 40), 14);
		return true;
	}

	bool HasRequiredControls(UWidgetBlueprint* Blueprint, bool bIncludeElectric, bool bIncludeAll = false)
	{
		UWidgetTree* Tree = Blueprint->WidgetTree;
		if (!Tree || !Cast<UCanvasPanel>(Tree->RootWidget) ||
			Tree->RootWidget->GetVisibility() != ESlateVisibility::SelfHitTestInvisible ||
			!Cast<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel")))) return false;
		for (const TCHAR* Name : {TEXT("CastleHealthText"), TEXT("CoreHealthText"), TEXT("Slot1Text"),
			TEXT("Slot2Text"), TEXT("PendingText"), TEXT("FailureText"), TEXT("StateText")})
			if (!Cast<UTextBlock>(Tree->FindWidget(FName(Name)))) return false;
		for (const TCHAR* Name : {TEXT("CastleHealthProgress"), TEXT("CoreHealthProgress"), TEXT("ReloadProgress")})
			if (!Cast<UProgressBar>(Tree->FindWidget(FName(Name)))) return false;
		for (const TCHAR* Name : {TEXT("Slot1Button"), TEXT("Slot2Button"), TEXT("BlastButton"),
			TEXT("ThrustButton"), TEXT("SwapButton"), TEXT("RestartButton")})
			if (!HasButton(Tree, FName(Name))) return false;
		for (const TCHAR* Name : FullButtonNames)
			if ((bIncludeAll || Tree->FindWidget(FName(Name))) && !HasButton(Tree, FName(Name))) return false;
		for (const TCHAR* Name : {TEXT("WarningText"), TEXT("ResourceText")})
			if (Tree->FindWidget(FName(Name)) && !Cast<UTextBlock>(Tree->FindWidget(FName(Name)))) return false;
		if ((bIncludeAll || Tree->FindWidget(TEXT("CombinationText"))) && !Cast<UTextBlock>(Tree->FindWidget(TEXT("CombinationText")))) return false;
		return !bIncludeElectric || HasButton(Tree, TEXT("ElectricButton"));
	}

	bool CanUpgradeFullControls(UWidgetTree* Tree)
	{
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(Tree->RootWidget);
		UCanvasPanel* Composition = Cast<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel")));
		if (!Canvas || !Composition || Composition->GetParent() != Canvas || !Cast<UCanvasPanelSlot>(Composition->Slot) ||
			!Cast<UBorder>(Tree->FindWidget(TEXT("CompositionBackground"))) ||
			!Cast<UTextBlock>(Tree->FindWidget(TEXT("ConfirmPrompt")))) return false;
		TSet<FName> OwnedNames;
		for (const TCHAR* Name : {TEXT("CompositionBackground"), TEXT("Slot1Button"), TEXT("Slot2Button"),
			TEXT("SwapButton"), TEXT("ConfirmPrompt"), TEXT("CombinationText")}) OwnedNames.Add(FName(Name));
		for (const TCHAR* Name : FullButtonNames) OwnedNames.Add(FName(Name));
		for (UWidget* Child : Composition->GetAllChildren())
			if (!OwnedNames.Contains(Child->GetFName()) || !Cast<UCanvasPanelSlot>(Child->Slot)) return false;
		for (FName Name : OwnedNames)
			if (UWidget* Widget = Tree->FindWidget(Name))
			{
				if (Widget->GetParent() != Composition || !Cast<UCanvasPanelSlot>(Widget->Slot)) return false;
			}
		for (const TCHAR* Name : FullButtonNames)
			if (!Tree->FindWidget(FName(Name)) && Tree->FindWidget(FName(*(FString(Name) + TEXT("Label"))))) return false;
		for (const TCHAR* Name : {TEXT("WarningText"), TEXT("ResourceText")})
			if (UWidget* Widget = Tree->FindWidget(FName(Name)))
				if (!Cast<UTextBlock>(Widget) || Widget->GetParent() != Canvas || !Cast<UCanvasPanelSlot>(Widget->Slot)) return false;
		return Tree->FindWidget(TEXT("CombinationText")) || !HasFullSelector(Tree) || CanAddCombinationDescription(Tree);
	}

	void UpgradeFullControls(UWidgetTree* Tree)
	{
		UCanvasPanel* Canvas = CastChecked<UCanvasPanel>(Tree->RootWidget);
		UCanvasPanel* Composition = CastChecked<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel")));
		auto Layout = [](UWidget* Widget, FVector2D Position, FVector2D Size,
			FVector2D Anchor = FVector2D::ZeroVector, FVector2D Alignment = FVector2D::ZeroVector)
		{
			UCanvasPanelSlot* Slot = CastChecked<UCanvasPanelSlot>(Widget->Slot);
			Slot->Modify();
			Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			Slot->SetAlignment(Alignment);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		};
		// 체력·장전·재시작의 수동 배치는 보존하고, 전체 선택판 소유 영역만 확장한다.
		Layout(Composition, FVector2D(0, -356), FVector2D(520, 216), FVector2D(.5, 1), FVector2D(.5, 0));
		Layout(Tree->FindWidget(TEXT("CompositionBackground")), FVector2D::ZeroVector, FVector2D(520, 216));
		Layout(Tree->FindWidget(TEXT("Slot1Button")), FVector2D(16, 12), FVector2D(236, 38));
		Layout(Tree->FindWidget(TEXT("Slot2Button")), FVector2D(268, 12), FVector2D(236, 38));
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FullButtonNames); ++Index)
		{
			const FVector2D Position(16 + (Index % 4) * 124, 60 + (Index / 4) * 48);
			UWidget* Button = Tree->FindWidget(FName(FullButtonNames[Index]));
			if (!Button) Button = AddButton(Tree, Composition, FName(FullButtonNames[Index]), FullButtonLabels[Index], Position, FVector2D(116, 40));
			Layout(Button, Position, FVector2D(116, 40));
		}
		Layout(Tree->FindWidget(TEXT("SwapButton")), FVector2D(16, 164), FVector2D(156, 36));
		Layout(Tree->FindWidget(TEXT("ConfirmPrompt")), FVector2D(194, 170), FVector2D(300, 28));
		if (!Tree->FindWidget(TEXT("WarningText")))
			AddText(Tree, Canvas, TEXT("WarningText"), TEXT(""), FVector2D(0, 26), FVector2D(640, 46), 16,
				FVector2D(.5, 0), FVector2D(.5, 0))->SetJustification(ETextJustify::Center);
		if (!Tree->FindWidget(TEXT("ResourceText")))
			AddText(Tree, Canvas, TEXT("ResourceText"), TEXT(""), FVector2D(0, 78), FVector2D(640, 24), 16,
				FVector2D(.5, 0), FVector2D(.5, 0))->SetJustification(ETextJustify::Center);
	}

	void AddElectricButton(UWidgetTree* Tree)
	{
		AddButton(Tree, CastChecked<UCanvasPanel>(Tree->FindWidget(TEXT("CompositionPanel"))),
			TEXT("ElectricButton"), TEXT("ϟ 전격"), FVector2D(348, 60), FVector2D(156, 42));
	}

	bool CompileWidget(UWidgetBlueprint* Blueprint, bool bIncludeElectric, bool bIncludeAll = false)
	{
		// 자식 레이블과 루트도 위젯 컴파일러가 사용하는 GUID를 갖는다.
		Blueprint->ForEachSourceWidget([Blueprint](UWidget* Widget)
		{
			if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
				Blueprint->OnVariableAdded(Widget->GetFName());
		});
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass || !HasRequiredControls(Blueprint, bIncludeElectric, bIncludeAll))
		{
			UE_LOG(LogTemp, Error, TEXT("Supply UI compilation or control validation failed"));
			return false;
		}
		Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>()->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Blueprint->MarkPackageDirty();
		return true;
	}
}
#endif

UObject* ULSShellSupplyEditorLibrary::BuildSupplyWidgetAsset(bool bIncludeElectric, bool bIncludeAll)
{
#if WITH_EDITOR
	const TCHAR* PackageName = TEXT("/Game/Landship/ShellExperiment/UI/WBP_ShellSupplyExperiment");
	const TCHAR* ObjectPath = TEXT("/Game/Landship/ShellExperiment/UI/WBP_ShellSupplyExperiment.WBP_ShellSupplyExperiment");
	UObject* Existing = LoadObject<UObject>(nullptr, ObjectPath);
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Existing);
	if (Existing)
	{
		if (!Blueprint || Blueprint->ParentClass != ULSShellSupplyExperimentWidget::StaticClass() ||
			!HasRequiredControls(Blueprint, false) || Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
		{
			UE_LOG(LogTemp, Error, TEXT("Existing Supply UI is invalid; preserving the asset"));
			return nullptr;
		}
		if (bIncludeAll)
		{
			if (!CanUpgradeFullControls(Blueprint->WidgetTree))
			{
				UE_LOG(LogTemp, Error, TEXT("Supply full selector controls differ from the expected canvas ownership; preserving the asset"));
				return nullptr;
			}
			if (HasRequiredControls(Blueprint, true, true)) return Blueprint;
			Blueprint->Modify();
			Blueprint->WidgetTree->Modify();
			if (!HasFullSelector(Blueprint->WidgetTree)) UpgradeFullControls(Blueprint->WidgetTree);
			if (!AddCombinationDescription(Blueprint->WidgetTree)) return nullptr;
			return CompileWidget(Blueprint, true, true) ? Blueprint : nullptr;
		}
		if (bIncludeElectric && !Blueprint->WidgetTree->FindWidget(TEXT("ElectricButton")))
		{
			Blueprint->Modify();
			Blueprint->WidgetTree->Modify();
			AddElectricButton(Blueprint->WidgetTree);
			if (!CompileWidget(Blueprint, true)) return nullptr;
		}
		return Blueprint;
	}
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UE_LOG(LogTemp, Error, TEXT("Existing Supply UI package could not be loaded; preserving the file"));
		return nullptr;
	}
	UPackage* Package = CreatePackage(PackageName);
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ULSShellSupplyExperimentWidget::StaticClass();
	Blueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), Package,
		TEXT("WBP_ShellSupplyExperiment"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Blueprint) return nullptr;
	FAssetRegistryModule::AssetCreated(Blueprint);
	UWidgetTree* Tree = Blueprint->WidgetTree;
	if (!Tree)
	{
		Tree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
		Blueprint->WidgetTree = Tree;
	}
	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SupplyCanvas"));
	Tree->RootWidget = Canvas;
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	AddBackground(Tree, Canvas, TEXT("CastleHealthBackground"), FVector2D(24, 20), FVector2D(280, 66));
	AddText(Tree, Canvas, TEXT("CastleHealthText"), TEXT("요새"), FVector2D(36, 28), FVector2D(256, 28));
	AddProgress(Tree, Canvas, TEXT("CastleHealthProgress"), FLinearColor(0.15f, 0.75f, 0.5f),
		FVector2D(36, 62), FVector2D(256, 10));
	AddBackground(Tree, Canvas, TEXT("CoreHealthBackground"), FVector2D(-24, 20), FVector2D(280, 66),
		FVector2D(1, 0), FVector2D(1, 0));
	AddText(Tree, Canvas, TEXT("CoreHealthText"), TEXT("동력로"), FVector2D(-292, 28), FVector2D(256, 28), 18, FVector2D(1, 0));
	AddProgress(Tree, Canvas, TEXT("CoreHealthProgress"), FLinearColor(0.95f, 0.3f, 0.2f),
		FVector2D(-292, 62), FVector2D(256, 10), FVector2D(1, 0));
	AddBackground(Tree, Canvas, TEXT("LoadoutBackground"), FVector2D(0, -124), FVector2D(520, 104),
		FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddText(Tree, Canvas, TEXT("Slot1Text"), TEXT("1  ➜ 추진"), FVector2D(-238, -114), FVector2D(220, 28), 21, FVector2D(0.5, 1));
	AddText(Tree, Canvas, TEXT("Slot2Text"), TEXT("2  ✹ 폭발"), FVector2D(22, -114), FVector2D(220, 28), 21, FVector2D(0.5, 1));
	AddProgress(Tree, Canvas, TEXT("ReloadProgress"), FLinearColor(0.3f, 0.75f, 1),
		FVector2D(-238, -76), FVector2D(476, 10), FVector2D(0.5, 1));
	AddText(Tree, Canvas, TEXT("ComposePrompt"), TEXT("F  조합"), FVector2D(-238, -55), FVector2D(130, 26), 18, FVector2D(0.5, 1));
	UTextBlock* Pending = AddText(Tree, Canvas, TEXT("PendingText"), TEXT(""), FVector2D(65, -55),
		FVector2D(180, 26), 18, FVector2D(0.5, 1));
	Pending->SetColorAndOpacity(FSlateColor(FLinearColor(1, 0.8f, 0.2f)));
	UCanvasPanel* Composition = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CompositionPanel"));
	Composition->bIsVariable = true;
	Composition->SetVisibility(ESlateVisibility::Collapsed);
	Place(Canvas, Composition, FVector2D(0, -320), FVector2D(520, 180), FVector2D(0.5, 1), FVector2D(0.5, 0));
	AddBackground(Tree, Composition, TEXT("CompositionBackground"), FVector2D::ZeroVector, FVector2D(520, 180));
	AddButton(Tree, Composition, TEXT("Slot1Button"), TEXT("▶ 1  ➜ 추진"), FVector2D(16, 12), FVector2D(236, 38));
	AddButton(Tree, Composition, TEXT("Slot2Button"), TEXT("2  ✹ 폭발"), FVector2D(268, 12), FVector2D(236, 38));
	AddButton(Tree, Composition, TEXT("BlastButton"), TEXT("✹ 폭발"), FVector2D(16, 60), FVector2D(156, 42));
	AddButton(Tree, Composition, TEXT("ThrustButton"), TEXT("➜ 추진"), FVector2D(182, 60), FVector2D(156, 42));
	if (bIncludeElectric) AddElectricButton(Tree);
	AddButton(Tree, Composition, TEXT("SwapButton"), TEXT("R  ⇄ 교환"), FVector2D(16, 116), FVector2D(156, 42));
	AddText(Tree, Composition, TEXT("ConfirmPrompt"), TEXT("F  확정 · 0.2×"), FVector2D(194, 124), FVector2D(300, 28));
	AddButton(Tree, Canvas, TEXT("RestartButton"), TEXT("다시 시작"), FVector2D(-24, -64), FVector2D(156, 42),
		FVector2D(1, 1), FVector2D(1, 0));
	UTextBlock* State = AddText(Tree, Canvas, TEXT("StateText"), TEXT(""), FVector2D(0, 110), FVector2D(640, 160),
		24, FVector2D(0.5, 0), FVector2D(0.5, 0));
	State->SetJustification(ETextJustify::Center);
	State->SetAutoWrapText(true);
	State->SetVisibility(ESlateVisibility::Collapsed);
	UTextBlock* Failure = AddText(Tree, Canvas, TEXT("FailureText"), TEXT(""), FVector2D(0, 28), FVector2D(460, 28),
		18, FVector2D(0.5, 0.5), FVector2D(0.5, 0));
	Failure->SetJustification(ETextJustify::Center);
	Failure->SetColorAndOpacity(FSlateColor(FLinearColor(1, 0.65f, 0.2f)));
	if (bIncludeAll)
	{
		UpgradeFullControls(Tree);
		if (!AddCombinationDescription(Tree)) return nullptr;
	}
	return CompileWidget(Blueprint, bIncludeElectric || bIncludeAll, bIncludeAll) ? Blueprint : nullptr;
#else
	return nullptr;
#endif
}
