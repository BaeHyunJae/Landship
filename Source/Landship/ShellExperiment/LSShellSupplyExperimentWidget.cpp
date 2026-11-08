#include "LSShellSupplyExperimentWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ProgressBar.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LSShellExperimentController.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellExperimentGameMode.h"
#include "LSShellReactiveComponent.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor PendingColor(1, 0.8f, 0.2f);
	const FLinearColor SelectionColor(.25f, .65f, 1);

	FString ModuleLabel(ELSShellModule Module)
	{
		switch (Module)
		{
		case ELSShellModule::Blast: return TEXT("✹ 폭발");
		case ELSShellModule::Thrust: return TEXT("➜ 추진");
		case ELSShellModule::Electric: return TEXT("전격");
		case ELSShellModule::Gravity: return TEXT("중력");
		case ELSShellModule::Split: return TEXT("분열");
		case ELSShellModule::Iron: return TEXT("철구");
		case ELSShellModule::Adhesion: return TEXT("접착");
		case ELSShellModule::Cooling: return TEXT("냉각");
		default: return TEXT("?");
		}
	}

	void SetText(UTextBlock* Block, const FString& Text)
	{
		if (Block) Block->SetText(FText::FromString(Text));
	}

	FText ModulePurpose(ELSShellModule Module)
	{
		if (Module == ELSShellModule::Thrust) return FText::FromString(TEXT("맞힌 물체를 수평으로 민다. 후행 대상이 사라지면 원래 착탄점 주변을 같은 방향으로 민다. 첫 슬롯은 직격 피해가 없다."));
		if (Module == ELSShellModule::Blast) return FText::FromString(TEXT("착탄 주변에 피해를 주고 위로 띄운다."));
		return FText::GetEmpty();
	}

	FString ModuleAction(ELSShellModule Module, float CoolingSeconds = 4, bool bFirstSlot = false, bool bArena = false)
	{
		switch (Module)
		{
		case ELSShellModule::Blast: return TEXT("폭발 · 주변 피해·위로 띄움");
		case ELSShellModule::Gravity: return bArena ? TEXT("중력 · 집결 후 해제") : TEXT("중력 · 고정된 장으로 몸체 흡인");
		case ELSShellModule::Iron: return TEXT("철구 · 새 철구 생성·발사");
		case ELSShellModule::Thrust: if (bArena) return TEXT("추진 · 수평 충격·직접 피해 0");
			return bFirstSlot ? TEXT("추진 · 수평 충격·직격 피해 0") : TEXT("추진 · 수평으로 몸체를 밈");
		case ELSShellModule::Electric: return TEXT("전격 · 저장 후 새 접촉 때 방전");
		case ELSShellModule::Adhesion: return TEXT("접착 · 몸체의 실제 접촉을 결속");
		case ELSShellModule::Cooling: return bArena
			? FString::Printf(TEXT("냉각 %.1f초 · 범위 냉각·직접 피해 0"), CoolingSeconds)
			: FString::Printf(TEXT("냉각 %.1f초 · 행동 중단·미끄럼·취성"), CoolingSeconds);
		case ELSShellModule::Split: return TEXT("분열 · 선행 효과를 세 자탄에 나눔");
		}
		return FString();
	}

	FString CombinationDescription(const ALSShellExperimentCoordinator* Coordinator)
	{
		const ELSShellModule First = Coordinator->GetModule(0), Second = Coordinator->GetModule(1);
		const bool bArena = Coordinator->IsArenaExperiment();
		if (First == ELSShellModule::Split)
			return FString::Printf(TEXT("1 착탄: 분열 · 세 자탄에 %s 전달\n2 자탄 명중: %s"),
				LSShellModuleLabel(Second), *ModuleAction(Second, 4.f / 3, false, bArena));
		const FString FirstLine = TEXT("1 착탄: ") + ModuleAction(First, Second == ELSShellModule::Split ? 2.f : 4.f, true, bArena);
		FString SecondAction = ModuleAction(Second, 4.f, false, bArena);
		if (Second == ELSShellModule::Split)
		{
			if (First == ELSShellModule::Iron) SecondAction = TEXT("분열 · 생성 철구를 세 몸체로 나눔");
			else if (First == ELSShellModule::Electric) SecondAction = TEXT("분열 · 남은 전하를 세 자탄에 나눔");
			else if (First == ELSShellModule::Gravity) SecondAction = TEXT("분열 · 남은 흡인을 세 자탄에 나눔");
		}
		return FirstLine + FString::Printf(TEXT("\n2 %.1f초 뒤/Space: %s"), Coordinator->GetConfiguredFollowupDelay(), *SecondAction);
	}

	void SetHealth(UProgressBar* Bar, UTextBlock* Text, const TCHAR* Name, const FLSHealthSnapshot& Health)
	{
		if (Bar) Bar->SetPercent(Health.MaxHealth > 0 ? Health.CurrentHealth / Health.MaxHealth : 0);
		SetText(Text, FString::Printf(TEXT("%s  %.0f / %.0f"), Name, Health.CurrentHealth, Health.MaxHealth));
	}

	void UpdateModuleButton(UButton* Button, ELSShellModule Module, int32 Slot,
		const ALSShellExperimentCoordinator* Coordinator, bool bCanEdit)
	{
		if (!Button) return;
		const bool bAvailable = Coordinator && Coordinator->AvailableModules.Contains(Module);
		const bool bUnlocked = Coordinator && Coordinator->CanSelectModule(Module);
		const bool bSelected = Coordinator && Coordinator->GetModule(Slot) == Module;
		Button->SetVisibility(bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Button->SetIsEnabled(bCanEdit && bAvailable && bUnlocked && !bSelected);
		Button->SetBackgroundColor(bSelected ? SelectionColor : FLinearColor::White);
		SetText(Cast<UTextBlock>(Button->GetContent()), ModuleLabel(Module) +
			(bAvailable && !bUnlocked ? TEXT(" 잠김") : TEXT("")));
	}

	FVector2D WidgetPosition(const FGeometry& Geometry, const FGeometry& ViewportGeometry, FVector2D Position)
	{
		return Geometry.AbsoluteToLocal(ViewportGeometry.LocalToAbsolute(Position));
	}
}

void ULSShellSupplyExperimentWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (RestartButton) RestartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RestartExperiment);
	if (SwapButton) SwapButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SwapModules);
	if (Slot1Button) Slot1Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot1);
	if (Slot2Button) Slot2Button->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot2);
	if (BlastButton) BlastButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectBlast);
	if (ThrustButton) ThrustButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectThrust);
	if (ElectricButton) ElectricButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectElectric);
	if (GravityButton) GravityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectGravity);
	if (SplitButton) SplitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSplit);
	if (IronButton) IronButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectIron);
	if (AdhesionButton) AdhesionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectAdhesion);
	if (CoolingButton) CoolingButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectCooling);
	auto SetModuleTooltip = [](UButton* Button, ELSShellModule Module)
	{
		if (!Button) return;
		FString Purpose = ModulePurpose(Module).ToString();
		if (Purpose.IsEmpty()) Purpose = ModuleAction(Module);
		Button->SetToolTipText(FText::FromString(Purpose + TEXT("\n반대 슬롯의 모듈을 누르면 서로 교환한다.")));
	};
	SetModuleTooltip(BlastButton, ELSShellModule::Blast);
	SetModuleTooltip(ThrustButton, ELSShellModule::Thrust);
	SetModuleTooltip(ElectricButton, ELSShellModule::Electric);
	SetModuleTooltip(GravityButton, ELSShellModule::Gravity);
	SetModuleTooltip(SplitButton, ELSShellModule::Split);
	SetModuleTooltip(IronButton, ELSShellModule::Iron);
	SetModuleTooltip(AdhesionButton, ELSShellModule::Adhesion);
	SetModuleTooltip(CoolingButton, ELSShellModule::Cooling);
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (Coordinator && Coordinator->IsArenaExperiment())
	{
		if (ThrustButton) ThrustButton->SetToolTipText(FText::FromString(TEXT("몸체를 수평으로 밀며 직접 피해는 주지 않는다.")));
		if (CoolingButton) CoolingButton->SetToolTipText(FText::FromString(TEXT("착탄 주변 몸체를 냉각하며 직접 피해는 주지 않는다.")));
		if (GravityButton) GravityButton->SetToolTipText(FText::FromString(TEXT("몸체를 모은 뒤 후행 효과를 발동한다.")));
	}
	RefreshState();
}

void ULSShellSupplyExperimentWidget::NativeDestruct()
{
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->CancelCompose();
	if (RestartButton) RestartButton->OnClicked.RemoveDynamic(this, &ThisClass::RestartExperiment);
	if (SwapButton) SwapButton->OnClicked.RemoveDynamic(this, &ThisClass::SwapModules);
	if (Slot1Button) Slot1Button->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot1);
	if (Slot2Button) Slot2Button->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot2);
	if (BlastButton) BlastButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectBlast);
	if (ThrustButton) ThrustButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectThrust);
	if (ElectricButton) ElectricButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectElectric);
	if (GravityButton) GravityButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectGravity);
	if (SplitButton) SplitButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSplit);
	if (IronButton) IronButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectIron);
	if (AdhesionButton) AdhesionButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectAdhesion);
	if (CoolingButton) CoolingButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectCooling);
	Super::NativeDestruct();
}

void ULSShellSupplyExperimentWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshState();
}

void ULSShellSupplyExperimentWidget::RefreshState()
{
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	const bool bCanEdit = Coordinator && Controller && Controller->CanEditComposition();
	const bool bComposing = Coordinator && Coordinator->IsComposing();
	if (RestartButton) RestartButton->SetIsEnabled(Coordinator != nullptr);
	if (!bComposing) ActiveSlot = 0;
	if (CompositionPanel) CompositionPanel->SetVisibility(bComposing ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	SetText(CombinationText, bComposing ? CombinationDescription(Coordinator) : FString());
	if (Slot1Button) Slot1Button->SetIsEnabled(bCanEdit && ActiveSlot != 0);
	if (Slot2Button) Slot2Button->SetIsEnabled(bCanEdit && ActiveSlot != 1);
	if (Slot1Button) Slot1Button->SetBackgroundColor(ActiveSlot == 0 ? SelectionColor : FLinearColor::White);
	if (Slot2Button) Slot2Button->SetBackgroundColor(ActiveSlot == 1 ? SelectionColor : FLinearColor::White);
	if (SwapButton) SwapButton->SetIsEnabled(bCanEdit);
	UpdateModuleButton(BlastButton, ELSShellModule::Blast, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(ThrustButton, ELSShellModule::Thrust, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(ElectricButton, ELSShellModule::Electric, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(GravityButton, ELSShellModule::Gravity, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(SplitButton, ELSShellModule::Split, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(IronButton, ELSShellModule::Iron, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(AdhesionButton, ELSShellModule::Adhesion, ActiveSlot, Coordinator, bCanEdit);
	UpdateModuleButton(CoolingButton, ELSShellModule::Cooling, ActiveSlot, Coordinator, bCanEdit);
	SetText(WarningText, Coordinator && Coordinator->IsBattleActive() ? Coordinator->GetEventText() : FString());
	SetText(ResourceText, Coordinator && Coordinator->IsBattleActive() ? Coordinator->GetMaterialStatusText() : FString());
	SetText(PendingText, Coordinator && Coordinator->CanManualTrigger() ? Coordinator->GetPendingEffectText() : TEXT(""));
	const FString Failure = Coordinator ? Coordinator->GetTargetFailureText() : FString();
	SetText(FailureText, !Failure.IsEmpty() ? Failure : Coordinator ? Coordinator->GetReactionText() : FString());
	if (FailureText) FailureText->SetColorAndOpacity(FSlateColor(!Failure.IsEmpty()
		? FLinearColor(1, .35f, .15f) : FLinearColor(.9f, .97f, 1)));
	if (ReloadProgress) ReloadProgress->SetPercent(Coordinator ? Coordinator->GetReloadFraction() : 0);
	if (!Coordinator)
	{
		const ALSShellExperimentGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSShellExperimentGameMode>() : nullptr;
		SetText(StateText, FString(TEXT("구성 오류\n")) + (Mode && !Mode->IsConfigurationValid()
			? Mode->GetConfigurationError() : TEXT("실험 진행 관리자가 없다.")));
		if (StateText) StateText->SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}
	SetHealth(CastleHealthProgress, CastleHealthText, TEXT("요새"), Coordinator->GetCastleHealth());
	SetHealth(CoreHealthProgress, CoreHealthText, TEXT("동력로"), Coordinator->GetCoreHealth());
	SetText(Slot1Text, TEXT("1  ") + ModuleLabel(Coordinator->GetModule(0)));
	SetText(Slot2Text, TEXT("2  ") + ModuleLabel(Coordinator->GetModule(1)));
	SetText(Cast<UTextBlock>(Slot1Button ? Slot1Button->GetContent() : nullptr),
		FString(ActiveSlot == 0 ? TEXT("▶ 1  ") : TEXT("1  ")) + ModuleLabel(Coordinator->GetModule(0)));
	SetText(Cast<UTextBlock>(Slot2Button ? Slot2Button->GetContent() : nullptr),
		FString(ActiveSlot == 1 ? TEXT("▶ 2  ") : TEXT("2  ")) + ModuleLabel(Coordinator->GetModule(1)));
	const FString Error = Coordinator->GetConfigurationError();
	const bool bShowState = !Error.IsEmpty() || !Coordinator->IsBattleActive();
	SetText(StateText, !Error.IsEmpty() ? FString(TEXT("구성 오류\n")) + Error : Coordinator->GetStatusText());
	if (StateText) StateText->SetVisibility(bShowState ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void ULSShellSupplyExperimentWidget::RestartExperiment()
{
	ActiveSlot = 0;
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->RestartExperiment();
}

void ULSShellSupplyExperimentWidget::SwapModules()
{
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	if (Controller && Controller->CanEditComposition())
		if (ALSShellExperimentCoordinator* Coordinator = Controller->GetCoordinator()) Coordinator->SwapModules();
	RefreshState();
}

void ULSShellSupplyExperimentWidget::SelectModule(ELSShellModule Module)
{
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	if (Controller && Controller->CanEditComposition())
		if (ALSShellExperimentCoordinator* Coordinator = Controller->GetCoordinator())
			if (Coordinator->CanSelectModule(Module)) Coordinator->SelectModule(ActiveSlot, Module);
	RefreshState();
}

void ULSShellSupplyExperimentWidget::SelectSlot1() { ActiveSlot = 0; RefreshState(); }
void ULSShellSupplyExperimentWidget::SelectSlot2() { ActiveSlot = 1; RefreshState(); }
void ULSShellSupplyExperimentWidget::SelectBlast() { SelectModule(ELSShellModule::Blast); }
void ULSShellSupplyExperimentWidget::SelectThrust() { SelectModule(ELSShellModule::Thrust); }
void ULSShellSupplyExperimentWidget::SelectElectric() { SelectModule(ELSShellModule::Electric); }
void ULSShellSupplyExperimentWidget::SelectGravity() { SelectModule(ELSShellModule::Gravity); }
void ULSShellSupplyExperimentWidget::SelectSplit() { SelectModule(ELSShellModule::Split); }
void ULSShellSupplyExperimentWidget::SelectIron() { SelectModule(ELSShellModule::Iron); }
void ULSShellSupplyExperimentWidget::SelectAdhesion() { SelectModule(ELSShellModule::Adhesion); }
void ULSShellSupplyExperimentWidget::SelectCooling() { SelectModule(ELSShellModule::Cooling); }

int32 ULSShellSupplyExperimentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 LastLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	const ALSShellExperimentCoordinator* Coordinator = Controller ? Controller->GetCoordinator() : nullptr;
	if (!Coordinator || !Coordinator->IsBattleActive()) return LastLayer;
	const FGeometry ViewportGeometry = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
	// 페인트 좌표에는 창 위치가 빠져 있다. 두 도형을 같은 데스크톱 틱 좌표로 맞춘다.
	const FGeometry WidgetTickGeometry = GetTickSpaceGeometry();
	auto DrawLines = [&](const TArray<FVector2D>& Points, FLinearColor Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LastLayer + 1, AllottedGeometry.ToPaintGeometry(),
			Points, ESlateDrawEffect::None, Color, true, Thickness);
	};
	FVector Aim;
	FVector2D Projected;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const ULSShellReactiveComponent* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>();
		if (!Reactive || !Reactive->IsPhysicalBodyPresent() || Reactive->GetStoredCharge() <= 0) continue;
		const UPrimitiveComponent* Body = Reactive->GetPhysicalBody();
		const FVector Point = Body->Bounds.Origin + FVector(0, 0, Body->Bounds.BoxExtent.Z + 20);
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Point, Projected, false)) continue;
		const FVector2D Center = WidgetPosition(WidgetTickGeometry, ViewportGeometry, Projected);
		const float Charge = Reactive->GetStoredCharge();
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const FVector2D Corner = Center + FVector2D(-24 + Index * 8, 0);
			DrawLines({Corner, Corner + FVector2D(5, 0), Corner + FVector2D(5, 7),
				Corner + FVector2D(0, 7), Corner}, Charge > Index * 60 ? FLinearColor(.2f, .85f, 1)
				: FLinearColor(.15f, .25f, .3f), 2);
		}
		FSlateDrawElement::MakeText(OutDrawElements, LastLayer + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(40, 18), FSlateLayoutTransform(Center + FVector2D(28, -5))),
			Charge < 1 ? FString(TEXT("<1")) : FString::Printf(TEXT("%.0f"), Charge), FCoreStyle::GetDefaultFontStyle("Bold", 12),
			ESlateDrawEffect::None, FLinearColor(.65f, .95f, 1));
	}
	for (const FVector& Point : Coordinator->GetShieldPoints())
		if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Point, Projected, false))
		{
			const FVector2D Center = WidgetPosition(WidgetTickGeometry, ViewportGeometry, Projected);
			DrawLines({Center + FVector2D(-8, -10), Center + FVector2D(8, -10), Center + FVector2D(8, 0),
				Center + FVector2D(0, 10), Center + FVector2D(-8, 0), Center + FVector2D(-8, -10)},
				FLinearColor(.65f, .85f, 1), 2);
		}
	if (Controller->GetAimPoint(Aim) &&
		UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Aim, Projected, false))
	{
		const FVector2D Center = WidgetPosition(WidgetTickGeometry, ViewportGeometry, Projected);
		TArray<FVector2D> Ring;
		for (int32 Index = 0; Index <= 24; ++Index)
		{
			const float Angle = Index * 2 * PI / 24;
			Ring.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 12);
		}
		DrawLines(Ring, Coordinator->GetReloadFraction() >= 1 && !Coordinator->IsComposing()
			? FLinearColor(0.3f, 1, 0.65f) : PendingColor, 2);
		FVector ChargePoint;
		if (Coordinator->IsArenaExperiment() && (Coordinator->GetModule(0) == ELSShellModule::Electric || Coordinator->GetModule(1) == ELSShellModule::Electric) &&
			Coordinator->GetPredictedChargeTarget(Aim, ChargePoint) &&
			UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), ChargePoint, Projected, false))
		{
			const FVector2D ChargeCenter = WidgetPosition(WidgetTickGeometry, ViewportGeometry, Projected);
			DrawLines({ChargeCenter + FVector2D(-7, -19), ChargeCenter + FVector2D(1, -27),
				ChargeCenter + FVector2D(-1, -20), ChargeCenter + FVector2D(7, -20),
				ChargeCenter + FVector2D(-2, -11), ChargeCenter + FVector2D(0, -18),
				ChargeCenter + FVector2D(-7, -19)}, FLinearColor(.2f, .9f, 1), 2);
		}
	}
	FVector PendingLocation;
	if (!Coordinator->CanManualTrigger() || !Coordinator->GetPendingEffectLocation(PendingLocation)) return LastLayer + 1;
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		GetOwningPlayer(), PendingLocation, Projected, false);
	FVector2D Center = bProjected ? WidgetPosition(WidgetTickGeometry, ViewportGeometry, Projected) : FVector2D::ZeroVector;
	const FVector2D Min(44, 100), Max(Size.X - 44, Size.Y - 150);
	const bool bOffscreen = !bProjected || Center.X < Min.X || Center.X > Max.X || Center.Y < Min.Y || Center.Y > Max.Y;
	if (bOffscreen)
	{
		const FVector2D ScreenCenter = (Min + Max) * 0.5f;
		FVector2D Direction = Center - ScreenCenter;
		if (!bProjected)
		{
			FVector CameraLocation;
			FRotator CameraRotation;
			Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
			const FVector CameraPoint = CameraRotation.UnrotateVector(PendingLocation - CameraLocation);
			Direction = FVector2D(CameraPoint.Y, -CameraPoint.Z);
		}
		Direction = Direction.GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = FVector2D(0, 1);
		const FVector2D Extent = (Max - Min) * 0.5f;
		const float ScaleX = FMath::Abs(Direction.X) > UE_SMALL_NUMBER ? Extent.X / FMath::Abs(Direction.X) : BIG_NUMBER;
		const float ScaleY = FMath::Abs(Direction.Y) > UE_SMALL_NUMBER ? Extent.Y / FMath::Abs(Direction.Y) : BIG_NUMBER;
		Center = ScreenCenter + Direction * FMath::Min(ScaleX, ScaleY);
		const FVector2D Side(-Direction.Y, Direction.X);
		const FVector2D Tip = Center + Direction * 14;
		DrawLines({Tip - Direction * 10 + Side * 6, Tip, Tip - Direction * 10 - Side * 6}, PendingColor, 3);
	}
	DrawLines({Center + FVector2D(0, -9), Center + FVector2D(9, 0), Center + FVector2D(0, 9),
		Center + FVector2D(-9, 0), Center + FVector2D(0, -9)}, PendingColor, 2);
	const FVector2D LabelPosition = Center + FVector2D(-22, 14);
	FSlateDrawElement::MakeText(OutDrawElements, LastLayer + 1,
		AllottedGeometry.ToPaintGeometry(FVector2D(50, 20), FSlateLayoutTransform(LabelPosition)),
		TEXT("Space"), FCoreStyle::GetDefaultFontStyle("Bold", 14), ESlateDrawEffect::None, PendingColor);
	return LastLayer + 1;
}
