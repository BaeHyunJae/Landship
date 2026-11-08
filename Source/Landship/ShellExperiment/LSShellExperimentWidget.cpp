#include "LSShellExperimentWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "LSShellExperimentController.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellExperimentGameMode.h"
#include "Rendering/DrawElements.h"

namespace
{
	FString ModuleName(ELSShellModule Module)
	{
		switch (Module)
		{
		case ELSShellModule::Gravity: return TEXT("중력");
		case ELSShellModule::Blast: return TEXT("폭발");
		case ELSShellModule::Split: return TEXT("분열");
		default: return FString();
		}
	}

	void SetText(UTextBlock* Block, const FString& Value)
	{
		if (Block) Block->SetText(FText::FromString(Value));
	}

	void UpdateModuleButton(UButton* Button, int32 Slot, ELSShellModule Module,
		const ALSShellExperimentCoordinator* Coordinator, bool bCanEdit)
	{
		if (!Button) return;
		Button->SetVisibility(Coordinator && Coordinator->IsComposing() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		const bool bUnlocked = Module != ELSShellModule::Split || (Coordinator && Coordinator->IsSplitUnlocked());
		const bool bSelected = Coordinator && Coordinator->GetModule(Slot) == Module;
		Button->SetIsEnabled(bCanEdit && bUnlocked && !bSelected);
		FString Label = FString::Printf(TEXT("%d · %s"), Slot + 1, *ModuleName(Module));
		if (!bUnlocked) Label += TEXT(" [잠김]");
		else if (bSelected) Label += TEXT(" [선택]");
		else if (Coordinator && Coordinator->GetModule(1 - Slot) == Module) Label += TEXT(" [교환]");
		SetText(Cast<UTextBlock>(Button->GetContent()), Label);
		Button->SetToolTipText(FText::FromString(!bUnlocked ? TEXT("중장갑 병기를 격파하면 분열 모듈이 해금된다.") :
			bSelected ? TEXT("이 슬롯에 선택한 모듈이다.") :
			Coordinator && Coordinator->GetModule(1 - Slot) == Module ? TEXT("다른 슬롯과 모듈 순서를 바꾼다.") :
			TEXT("이 슬롯의 모듈을 바꾼다. 이미 발사한 포탄은 바뀌지 않는다.")));
	}
}

void ULSShellExperimentWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (RestartButton)
	{
		RestartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RestartExperiment);
	}
	if (SwapButton)
	{
		SwapButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SwapModules);
	}
	if (Slot1GravityButton) Slot1GravityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot1Gravity);
	if (Slot1BlastButton) Slot1BlastButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot1Blast);
	if (Slot1SplitButton) Slot1SplitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot1Split);
	if (Slot2GravityButton) Slot2GravityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot2Gravity);
	if (Slot2BlastButton) Slot2BlastButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot2Blast);
	if (Slot2SplitButton) Slot2SplitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SelectSlot2Split);
	RefreshState();
}

void ULSShellExperimentWidget::NativeDestruct()
{
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->CancelCompose();
	if (RestartButton) RestartButton->OnClicked.RemoveDynamic(this, &ThisClass::RestartExperiment);
	if (SwapButton) SwapButton->OnClicked.RemoveDynamic(this, &ThisClass::SwapModules);
	if (Slot1GravityButton) Slot1GravityButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot1Gravity);
	if (Slot1BlastButton) Slot1BlastButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot1Blast);
	if (Slot1SplitButton) Slot1SplitButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot1Split);
	if (Slot2GravityButton) Slot2GravityButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot2Gravity);
	if (Slot2BlastButton) Slot2BlastButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot2Blast);
	if (Slot2SplitButton) Slot2SplitButton->OnClicked.RemoveDynamic(this, &ThisClass::SelectSlot2Split);
	Super::NativeDestruct();
}

void ULSShellExperimentWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshState();
}

void ULSShellExperimentWidget::RefreshState()
{
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	const bool bCanEdit = Controller && Controller->CanEditComposition();
	UpdateModuleButton(Slot1GravityButton, 0, ELSShellModule::Gravity, Coordinator, bCanEdit);
	UpdateModuleButton(Slot1BlastButton, 0, ELSShellModule::Blast, Coordinator, bCanEdit);
	UpdateModuleButton(Slot1SplitButton, 0, ELSShellModule::Split, Coordinator, bCanEdit);
	UpdateModuleButton(Slot2GravityButton, 1, ELSShellModule::Gravity, Coordinator, bCanEdit);
	UpdateModuleButton(Slot2BlastButton, 1, ELSShellModule::Blast, Coordinator, bCanEdit);
	UpdateModuleButton(Slot2SplitButton, 1, ELSShellModule::Split, Coordinator, bCanEdit);
	SetText(SplitStatusText, Coordinator && Coordinator->IsSplitUnlocked()
		? TEXT("분열 해금 · 두 슬롯에서 선택 가능") : TEXT("분열 잠김 · 중장갑 병기 격파 시 해금"));
	if (!Coordinator)
	{
		const ALSShellExperimentGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSShellExperimentGameMode>() : nullptr;
		SetText(StatusText, Mode && !Mode->IsConfigurationValid() ? Mode->GetConfigurationError() :
			TEXT("실험 진행 관리자가 맵에 없다."));
		if (SwapButton) SwapButton->SetIsEnabled(false);
		return;
	}
	const FLSHealthSnapshot Castle = Coordinator->GetCastleHealth();
	const FLSHealthSnapshot Core = Coordinator->GetCoreHealth();
	SetText(HealthText, FString::Printf(TEXT("요새 HP  %.0f / %.0f"), Castle.CurrentHealth, Castle.MaxHealth));
	SetText(CoreText, FString::Printf(TEXT("중앙 동력로 HP  %.0f / %.0f"), Core.CurrentHealth, Core.MaxHealth));
	SetText(LoadoutText, FString::Printf(TEXT("슬롯 1  %s  →  슬롯 2  %s%s"),
		*ModuleName(Coordinator->GetModule(0)), *ModuleName(Coordinator->GetModule(1)),
		Coordinator->IsComposing() ? TEXT("  [조합 중 · 0.2배속]") : TEXT("")));
	const float Reload = Coordinator->GetReloadFraction();
	if (ReloadProgress) ReloadProgress->SetPercent(Reload);
	FString Status = Coordinator->GetStatusText();
	if (Coordinator->IsBattleActive())
		Status += Reload >= 1 ? TEXT("  |  발사 준비 완료") : FString::Printf(TEXT("  |  재장전 %.0f%%"), Reload * 100);
	const FString PendingEffect = Coordinator->GetPendingEffectText();
	if (!PendingEffect.IsEmpty()) Status += TEXT("\n") + PendingEffect;
	if (!Coordinator->GetEventText().IsEmpty()) Status += TEXT("\n") + Coordinator->GetEventText();
	SetText(StatusText, Status);
	SetText(ReactionText, Coordinator->GetReactionText());
	FString Help = Coordinator->IsComposing()
		? TEXT("F 다시 누르기: 확정 · 슬롯 버튼: 선택 · R: 교환 · 창 전환: 취소")
		: TEXT("좌클릭: 발사 · F: 조합 열기/확정 · 조합 중 R: 순서 교환");
	Help += TEXT("\n가운데 드래그: 이동 · 휠: 확대/축소 · 중력은 병력·운반차를 끌어당긴다.");
	if (Coordinator->CanManualTrigger()) Help += TEXT("\nSpace: 후행 모듈 즉시 발동");
	if (Controller)
	{
		const FString AimHint = Controller->GetAimHint();
		if (!AimHint.IsEmpty()) Help += TEXT("\n조준: ") + AimHint;
	}
	SetText(HelpText, Help);
	if (SwapButton) SwapButton->SetIsEnabled(bCanEdit);
}

void ULSShellExperimentWidget::RestartExperiment()
{
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->RestartExperiment();
}

void ULSShellExperimentWidget::SwapModules()
{
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	if (Controller && Controller->CanEditComposition())
		if (ALSShellExperimentCoordinator* Coordinator = Controller->GetCoordinator()) Coordinator->SwapModules();
	RefreshState();
}

void ULSShellExperimentWidget::SelectModule(int32 SlotIndex, ELSShellModule Module)
{
	if ((SlotIndex != 0 && SlotIndex != 1) ||
		(Module != ELSShellModule::Gravity && Module != ELSShellModule::Blast && Module != ELSShellModule::Split)) return;
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	if (Controller && Controller->CanEditComposition())
	{
		if (ALSShellExperimentCoordinator* Coordinator = Controller->GetCoordinator())
			if (Module != ELSShellModule::Split || Coordinator->IsSplitUnlocked()) Coordinator->SelectModule(SlotIndex, Module);
	}
	RefreshState();
}

void ULSShellExperimentWidget::SelectSlot1Gravity() { SelectModule(0, ELSShellModule::Gravity); }
void ULSShellExperimentWidget::SelectSlot1Blast() { SelectModule(0, ELSShellModule::Blast); }
void ULSShellExperimentWidget::SelectSlot1Split() { SelectModule(0, ELSShellModule::Split); }
void ULSShellExperimentWidget::SelectSlot2Gravity() { SelectModule(1, ELSShellModule::Gravity); }
void ULSShellExperimentWidget::SelectSlot2Blast() { SelectModule(1, ELSShellModule::Blast); }
void ULSShellExperimentWidget::SelectSlot2Split() { SelectModule(1, ELSShellModule::Split); }

int32 ULSShellExperimentWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 LastLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const ALSShellExperimentController* Controller = Cast<ALSShellExperimentController>(GetOwningPlayer());
	FVector Aim;
	FVector2D ViewportPosition;
	if (!Controller || !Controller->GetAimPoint(Aim) ||
		!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Aim, ViewportPosition, false))
		return LastLayer;
	const FVector2D Center = ViewportPosition;
	TArray<FVector2D> Ring;
	for (int32 Index = 0; Index <= 24; ++Index)
	{
		const float Angle = Index * 2 * PI / 24;
		Ring.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 12);
	}
	const ALSShellExperimentCoordinator* Coordinator = Controller->GetCoordinator();
	const FLinearColor Color = Coordinator && Coordinator->GetReloadFraction() >= 1 && !Coordinator->IsComposing()
		? FLinearColor(0.3f, 1, 0.65f) : FLinearColor(1, 0.65f, 0.15f);
	FSlateDrawElement::MakeLines(OutDrawElements, LastLayer + 1, AllottedGeometry.ToPaintGeometry(),
		Ring, ESlateDrawEffect::None, Color, true, 2);
	return LastLayer + 1;
}
