#include "HUD/LSBattleHUDWidget.h"
#include "HUD/LSCommandButton.h"
#include "Castle/LSCastle.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Match/LSGameState.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Vision/LSVisionSubsystem.h"
#include "Widgets/SViewport.h"

namespace
{
FText Text(const FString& Value)
{
	return FText::FromString(Value);
}
FString OrderName(ELSOrderKind Order)
{
	switch (Order)
	{
	case ELSOrderKind::Move:
		return TEXT("이동");
	case ELSOrderKind::AttackMove:
		return TEXT("공격 이동");
	case ELSOrderKind::AttackTarget:
		return TEXT("지정 공격");
	case ELSOrderKind::Hold:
		return TEXT("위치 사수");
	default:
		return TEXT("대기");
	}
}
const TCHAR* TroopNames[] = {TEXT("창병"), TEXT("궁병"), TEXT("기마병"), TEXT("의무병"),
    TEXT("방패병"), TEXT("중화기병"), TEXT("자폭병")};
static_assert(UE_ARRAY_COUNT(TroopNames) == ALSTroopUnit::GetTroopTypeCount());
} // namespace

void ULSBattleHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	for (int32 Index = 0; Index < ULSCommandCardComponent::CellCount; ++Index)
		if (auto* Button = Cast<ULSCommandButton>(GetWidgetFromName(*FString::Printf(TEXT("Command%d"), Index))))
			Button->BindCell(this, Index);
	RefreshState();
}

FText ULSBattleHUDWidget::GetSelectionDescription() const
{
	const ALSPlayerController* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	const TArray<AActor*> Actors = PC ? PC->GetSelectedActors() : TArray<AActor*>();
	if (Actors.IsEmpty())
		return Text(TEXT("대상을 선택하세요"));
	AActor* Actor = Actors[0];
	const auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
	FString Name = TEXT("거점");
	if (Selection && Selection->Kind == ELSSelectionKind::Castle)
		Name = TEXT("이동 성");
	else if (const ALSTroopUnit* Unit = Cast<ALSTroopUnit>(Actor))
	{
		const int32 TypeIndex = static_cast<int32>(Unit->GetTroopType());
		Name = TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(TroopNames)
		    ? TroopNames[TypeIndex] : TEXT("알 수 없는 병종");
	}
	const auto* Health = Actor->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot HP;
	const FString Prefix = Actors.Num() > 1 ? TEXT("첫 대상 ") : TEXT("");
	FString Result = FString::Printf(TEXT("선택 %d개  |  %s%s%s"), Actors.Num(), *Prefix, *Name,
	                                 Selection && Selection->CanPlayerControl() ? TEXT("") : TEXT(" · 정보 확인"));
	if (Health && Health->TryGetHealth(HP))
		Result += FString::Printf(TEXT("\n체력 %.0f / %.0f"), HP.CurrentHealth, HP.MaxHealth);
	if (const auto* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
	    Orders && Selection && Selection->CanPlayerControl())
		Result +=
		    FString::Printf(TEXT("\n%s  |  예약 %d개"), *OrderName(Orders->GetOrderKind()), Orders->GetQueuedCount());
	if (const auto* Attack = Actor->FindComponentByClass<ULSBasicAttackComponent>(); Attack && Attack->IsConfigured())
		Result += FString::Printf(TEXT("\n공격 간격 %.2f초"), Attack->GetEffectiveAttackInterval());
	return Text(Result);
}

FText ULSBattleHUDWidget::GetCommandFailureText() const
{
	const auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	if (!PC || PC->GetSelectedActors().IsEmpty())
		return FText::GetEmpty();
	const auto* Orders = PC->GetSelectedActors()[0]->FindComponentByClass<ULSOrderComponent>();
	if (!Orders)
		return FText::GetEmpty();
	switch (Orders->GetLastFailure())
	{
	case ELSOrderFailure::BlockedByBarricade:
		return Text(TEXT("바리케이드로 경로 차단"));
	case ELSOrderFailure::Unreachable:
		return Text(TEXT("이동 가능한 경로를 찾지 못했습니다"));
	case ELSOrderFailure::QueueFull:
		return Text(TEXT("예약 명령이 가득 찼습니다"));
	case ELSOrderFailure::InvalidTarget:
		return Text(TEXT("공격할 수 없는 대상입니다"));
	case ELSOrderFailure::Unavailable:
		return Text(TEXT("현재 명령을 내릴 수 없습니다"));
	default:
		return FText::GetEmpty();
	}
}

FText ULSBattleHUDWidget::GetCastleHealthText() const
{
	const auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	const auto* Castle = PC ? PC->GetConfiguredBattlefieldCastle() : nullptr;
	const auto* Health = Castle ? Castle->FindComponentByClass<ULSHealthComponent>() : nullptr;
	FLSHealthSnapshot Snapshot;
	return Health && Health->TryGetHealth(Snapshot)
	           ? Text(FString::Printf(TEXT("이동 성  |  체력 %.0f / %.0f%s"), Snapshot.CurrentHealth,
	                                  Snapshot.MaxHealth, Snapshot.bIsDead ? TEXT(" · 파괴됨") : TEXT("")))
	           : Text(TEXT("이동 성  |  상태 확인 불가"));
}

float ULSBattleHUDWidget::GetCastleHealthFraction() const
{
	const auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	const auto* Castle = PC ? PC->GetConfiguredBattlefieldCastle() : nullptr;
	const auto* Health = Castle ? Castle->FindComponentByClass<ULSHealthComponent>() : nullptr;
	FLSHealthSnapshot Snapshot;
	return Health && Health->TryGetHealth(Snapshot) && Snapshot.MaxHealth > 0
	           ? FMath::Clamp(Snapshot.CurrentHealth / Snapshot.MaxHealth, 0.0f, 1.0f) : 0;
}

FLSCastleWeaponHUDState ULSBattleHUDWidget::GetCastleWeaponStatus(ELSCastleWeapon Weapon) const
{
	FLSCastleWeaponHUDState Result;
	const int32 Index = static_cast<int32>(Weapon);
	const TCHAR* Names[] = {TEXT("Q  일반 대포"), TEXT("W  산탄"), TEXT("E  발리스타")};
	if (Index < 0 || Index >= UE_ARRAY_COUNT(Names)) return Result;
	Result.Title = Text(Names[Index]);
	Result.Status = Text(TEXT("사용 불가"));
	const auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	const auto* Castle = PC ? PC->GetConfiguredBattlefieldCastle() : nullptr;
	if (!Castle) return Result;
	const auto* Battlefield = GetWorld() ? GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>() : nullptr;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()) ||
	    (!LSBattleOutcome::IsManaged(GetWorld()) && Battlefield && Battlefield->IsBattleEndedForEnemies()))
	{
		Result.Status = Text(TEXT("전투 종료"));
		return Result;
	}
	FLSCastleWeaponState State;
	if (!Castle->Weapons->GetWeaponState(Weapon, State)) return Result;
	Result.Remaining = Castle->Weapons->GetRemainingCooldown(Weapon);
	Result.Duration = Castle->Weapons->GetCooldownDuration(Weapon);
	Result.bAvailable = PC->GetCastleWeaponSource() == Castle &&
	                    Castle->Weapons->GetStatus(Weapon) == ELSWeaponCastResult::Ready;
	FString Status = !State.bUnlocked ? TEXT("잠김") : !State.bEnabled ? TEXT("비활성")
	    : PC->GetAimingCastle() == Castle && PC->GetPendingWeapon() == Weapon
	        ? PC->GetLastWeaponResult() == ELSWeaponCastResult::OutOfRange ? TEXT("사거리 밖") : TEXT("조준 중")
	    : Result.Remaining > 0 ? TEXT("재사용 대기") : Result.bAvailable ? TEXT("사용 가능") : TEXT("사용 불가");
	if (Result.Remaining > 0) Status += FString::Printf(TEXT(" %.1f초"), Result.Remaining);
	Result.Status = Text(Status);
	return Result;
}

void ULSBattleHUDWidget::SetText(const TCHAR* Name, const FText& Value)
{
	if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(Name)))
		Label->SetText(Value);
}

void ULSBattleHUDWidget::RefreshState()
{
	const auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	if (UWidget* Panel = GetWidgetFromName(TEXT("CastleStatusPanel")))
		Panel->SetVisibility(PC && PC->GetConfiguredBattlefieldCastle() ? ESlateVisibility::Visible
		                                                            : ESlateVisibility::Collapsed);
	if (!PC)
		return;
	SetText(TEXT("CastleHealthText"), GetCastleHealthText());
	if (auto* Bar = Cast<UProgressBar>(GetWidgetFromName(TEXT("CastleHealthProgress"))))
		Bar->SetPercent(GetCastleHealthFraction());
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const auto State = GetCastleWeaponStatus(static_cast<ELSCastleWeapon>(Index));
		SetText(*FString::Printf(TEXT("CastleWeaponTitle%d"), Index), State.Title);
		SetText(*FString::Printf(TEXT("CastleWeaponState%d"), Index), State.Status);
		if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(*FString::Printf(TEXT("CastleWeaponState%d"), Index))))
			Label->SetColorAndOpacity(FSlateColor(State.bAvailable ? FLinearColor(0.45f, 1, 0.65f)
			                                                       : FLinearColor(1, 0.77f, 0.35f)));
		if (auto* Bar = Cast<UProgressBar>(GetWidgetFromName(*FString::Printf(TEXT("CastleWeaponProgress%d"), Index))))
			Bar->SetPercent(State.Duration > 0 && State.Remaining > 0
			    ? FMath::Clamp(1 - State.Remaining / State.Duration, 0.0f, 1.0f) : State.bAvailable ? 1 : 0);
	}
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		bDiscoveryVisible = false;
		if (auto* Caption = GetWidgetFromName(TEXT("DiscoveryPanel")))
			Caption->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetText(TEXT("SelectionText"), GetSelectionDescription());
	SetText(TEXT("FailureText"), GetCommandFailureText());
	DisplayedCells = PC->CommandCard->BuildCard();
	SetText(TEXT("CommandGroupText"), PC->CommandCard->GetGroupText());
	const auto* State = GetWorld()->GetGameState<ALSGameState>();
	SetText(TEXT("PopulationText"), State ? Text(FString::Printf(TEXT("인구 %d / %d"), State->GetCurrentPopulation(),
	                                                             State->GetPopulationLimit()))
	                                      : FText::GetEmpty());
	SetText(TEXT("CommandHint"), LSBattleOutcome::AreActionsStopped(GetWorld())
	                                 ? Text(TEXT("전투 종료"))
	                                 : PC->GetPendingInput() == ELSCommandInput::None
	                                 ? Text(TEXT("버튼 또는 단축키로 명령 선택"))
	                                 : Text(TEXT("좌클릭 대상 지정 · 우클릭/Esc 취소")));
	for (int32 Index = 0; Index < DisplayedCells.Num(); ++Index)
	{
		const auto& Entry = DisplayedCells[Index].Entry;
		const bool bOccupied = !Entry.Id.IsNone();
		SetText(*FString::Printf(TEXT("CommandTitle%d"), Index), Entry.Title);
		SetText(*FString::Printf(TEXT("CommandKey%d"), Index),
		        bOccupied && Entry.Hotkey.IsValid() ? Entry.Hotkey.GetDisplayName() : FText::GetEmpty());
		FString Detail = Entry.Detail.ToString();
		if (Entry.Remaining > 0)
			Detail += FString::Printf(TEXT(" %0.1f초"), Entry.Remaining);
		SetText(*FString::Printf(TEXT("CommandState%d"), Index), Text(Detail));
		SetText(*FString::Printf(TEXT("CommandLevel%d"), Index),
		        Entry.Level > 0 ? Text(FString::Printf(TEXT("Lv %d"), Entry.Level)) : FText::GetEmpty());
		if (auto* Button = Cast<UButton>(GetWidgetFromName(*FString::Printf(TEXT("Command%d"), Index))))
		{
			Button->SetIsEnabled(Entry.bEnabled);
			Button->SetToolTipText(Entry.Tooltip);
			Button->SetBackgroundColor(bOccupied ? FLinearColor(0.12f, 0.3f, 0.4f)
			                                     : FLinearColor(0.035f, 0.06f, 0.09f));
		}
		if (auto* Bar = Cast<UProgressBar>(GetWidgetFromName(*FString::Printf(TEXT("CommandProgress%d"), Index))))
		{
			Bar->SetVisibility(bOccupied && Entry.Remaining > 0 ? ESlateVisibility::HitTestInvisible
			                                                    : ESlateVisibility::Hidden);
			Bar->SetPercent(Entry.Duration > 0 ? FMath::Clamp(1 - Entry.Remaining / Entry.Duration, 0.0f, 1.0f) : 0);
		}
	}
}

void ULSBattleHUDWidget::SetPanelLayout(const FBox2D& Info, const FBox2D& Commands)
{
	const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
	const TCHAR* Names[] = {TEXT("InfoPanel"), TEXT("CommandPanel")};
	const FBox2D Boxes[] = {Info, Commands};
	for (int32 Index = 0; Index < 2; ++Index)
		if (UWidget* Panel = GetWidgetFromName(Names[Index]))
			if (auto* CanvasSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
			{
				CanvasSlot->SetPosition(Boxes[Index].Min / DPI);
				CanvasSlot->SetSize(Boxes[Index].GetSize() / DPI);
			}
}

void ULSBattleHUDWidget::SetCastleStatusLayout(const FBox2D& Status)
{
	if (!Status.bIsValid) return;
	if (UWidget* Panel = GetWidgetFromName(TEXT("CastleStatusPanel")))
		if (auto* StatusSlot = Cast<UCanvasPanelSlot>(Panel->Slot))
		{
			const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
			StatusSlot->SetPosition(Status.Min / DPI);
			StatusSlot->SetSize(Status.GetSize() / DPI);
		}
}

void ULSBattleHUDWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	RefreshState();
	if (auto* Vision = GetWorld() ? GetWorld()->GetSubsystem<ULSVisionSubsystem>() : nullptr)
	{
		if (LSBattleOutcome::AreActionsStopped(GetWorld()))
			bDiscoveryVisible = false;
		else if (!bDiscoveryPainted && Vision->HasPendingDiscoveryCaption())
			bDiscoveryVisible = true;
		if (bDiscoveryPainted && GetWorld()->GetTimeSeconds() - DiscoveryStart >= DiscoveryDuration)
			bDiscoveryVisible = false;
		if (auto* Caption = GetWidgetFromName(TEXT("DiscoveryPanel")))
			Caption->SetVisibility(bDiscoveryVisible ? ESlateVisibility::HitTestInvisible
			                                         : ESlateVisibility::Collapsed);
	}
}

int32 ULSBattleHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
                                      FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
                                      bool bEnabled) const
{
	const int32 Result = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bEnabled);
	const UWidget* Caption = GetWidgetFromName(TEXT("DiscoveryPanel"));
	if (!LSBattleOutcome::AreActionsStopped(GetWorld()) && bDiscoveryVisible && !bDiscoveryPainted && Caption && Caption->IsVisible())
		if (auto* Vision = GetWorld() ? GetWorld()->GetSubsystem<ULSVisionSubsystem>() : nullptr)
		{
			bDiscoveryPainted = true;
			DiscoveryStart = GetWorld()->GetTimeSeconds();
			Vision->MarkDiscoveryCaptionDisplayed();
		}
	return Result;
}

FReply ULSBattleHUDWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	FReply Reply = FReply::Handled();
	if (auto* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		if (auto Viewport = Client->GetGameViewportWidget())
			Reply.SetUserFocus(Viewport.ToSharedRef(), EFocusCause::Mouse);
	return Reply;
}
bool ULSBattleHUDWidget::ExecuteCommandCell(int32 Index)
{
	auto* PC = Cast<ALSPlayerController>(GetOwningPlayer());
	if (!PC || !DisplayedCells.IsValidIndex(Index))
		return false;
	const bool bResult = PC->CommandCard->ExecuteCell(Index, DisplayedCells[Index]);
	RefreshState();
	return bResult;
}
