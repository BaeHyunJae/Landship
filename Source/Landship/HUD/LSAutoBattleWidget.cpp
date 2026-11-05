#include "HUD/LSAutoBattleWidget.h"
#include "Controls/LSAutoBattleController.h"
#include "Match/LSAutoBattleGameMode.h"
#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Match/LSAutoBattlePreset.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
const TCHAR* PhaseNames[] = {TEXT("전장 준비 중"), TEXT("전투 준비"), TEXT("성 직접 조종"),
                             TEXT("승리"),         TEXT("패배"),      TEXT("설정 오류")};
const TCHAR* WeaponNames[] = {TEXT("일반 대포 · 밀집한 적"), TEXT("산탄포 · 가까운 적"),
                              TEXT("발리스타 · 체력이 높은 적")};
const TCHAR* AutoBattleTroopButtons[] = {
    TEXT("Spear"), TEXT("Archer"), TEXT("Cavalry"), TEXT("Medic"), TEXT("Shieldbearer"), TEXT("Mortar"), TEXT("Bomber")};
const TCHAR* AutoBattleTroopLabels[] = {
    TEXT("창병"), TEXT("궁병"), TEXT("기마병"), TEXT("의무병"), TEXT("방패병"), TEXT("중화기병"), TEXT("자폭병")};
static_assert(UE_ARRAY_COUNT(AutoBattleTroopButtons) == ALSTroopUnit::GetTroopTypeCount());
static_assert(UE_ARRAY_COUNT(AutoBattleTroopLabels) == UE_ARRAY_COUNT(AutoBattleTroopButtons));
FString FormationCost(const TArray<FLSAutoBattlePlacement>& Formation, const ALSAutoBattleScenario* Scenario)
{
	if (!Scenario)
	{
		return TEXT("—");
	}
	int64 Total = 0;
	for (const auto& Unit : Formation)
	{
		const int32* Cost = Scenario->UnitCosts.Find(Unit.Type);
		if (!Cost || *Cost < 1)
		{
			return TEXT("—");
		}
		Total += *Cost;
	}
	return FText::AsNumber(Total).ToString();
}
FString CastleHealth(const ALSAutoBattleCastle* Castle)
{
	FLSHealthSnapshot Health;
	return IsValid(Castle) && Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health)
	           ? FString::Printf(TEXT("%.0f / %.0f"), Health.CurrentHealth, Health.MaxHealth)
	           : TEXT("—");
}
} // namespace

void ULSAutoBattleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureTroopButtons();
	SetupBattlefieldPicker();
	for (const TCHAR* Name : {TEXT("LimitTitle"), TEXT("BudgetLabel"), TEXT("PopulationLabel"),
	                          TEXT("BudgetInput"), TEXT("PopulationInput"), TEXT("ApplyLimits")})
	{
		if (UWidget* LegacyControl = GetWidgetFromName(Name))
			LegacyControl->RemoveFromParent();
	}
	const TCHAR* PresetControls[] = {TEXT("PresetTitle"), TEXT("PresetName"), TEXT("SaveNewPreset"),
	                                 TEXT("OverwritePreset"), TEXT("PresetList"), TEXT("LoadSelectedPreset"),
	                                 TEXT("ReloadPresetList"), TEXT("EditorMessage"), TEXT("PresetFolder")};
	const float PresetPositions[] = {264, 306, 354, 354, 404, 452, 452, 506, 588};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PresetControls); ++Index)
	{
		if (UWidget* Control = GetWidgetFromName(PresetControls[Index]))
		{
			if (auto* PresetSlot = Cast<UCanvasPanelSlot>(Control->Slot))
				PresetSlot->SetPosition(FVector2D(PresetSlot->GetPosition().X, PresetPositions[Index]));
		}
	}
	SetLabel(TEXT("SaveNewPresetLabel"), TEXT("새 이름 저장"));
	SetLabel(TEXT("OverwritePresetLabel"), TEXT("이름 덮어쓰기"));
	SetLabel(TEXT("Help"), TEXT("우클릭 유지: 회전 + WASD / Q·E 상하\n가운데 드래그: 화면 이동\n휠: 앞뒤 · 우클릭+휠: "
	                            "속도\nShift 가속 · Home 복귀"));
#define LS_BIND_BUTTON(Name, Method)                                                                                   \
	if (auto* Button = Cast<UButton>(GetWidgetFromName(TEXT(Name))))                                                   \
		Button->OnClicked.AddDynamic(this, &ThisClass::Method);
	LS_BIND_BUTTON("Spear", Spear)
	LS_BIND_BUTTON("Archer", Archer)
	LS_BIND_BUTTON("Cavalry", Cavalry)
	LS_BIND_BUTTON("Medic", Medic)
	LS_BIND_BUTTON("Shieldbearer", Shieldbearer)
	LS_BIND_BUTTON("Mortar", Mortar)
	LS_BIND_BUTTON("Bomber", Bomber)
	LS_BIND_BUTTON("Castle", Castle)
	LS_BIND_BUTTON("Cannon", Cannon)
	LS_BIND_BUTTON("Shotgun", Shotgun)
	LS_BIND_BUTTON("Ballista", Ballista)
	LS_BIND_BUTTON("Start", Start)
	LS_BIND_BUTTON("Retry", Retry)
	LS_BIND_BUTTON("Reset", Reset)
	LS_BIND_BUTTON("Remove", Remove)
	LS_BIND_BUTTON("EditAllies", EditAllies)
	LS_BIND_BUTTON("EditEnemies", EditEnemies)
	LS_BIND_BUTTON("SaveNewPreset", SaveNewPreset)
	LS_BIND_BUTTON("OverwritePreset", OverwritePreset)
	LS_BIND_BUTTON("LoadSelectedPreset", LoadSelectedPreset)
	LS_BIND_BUTTON("ReloadPresetList", ReloadPresetList)
#undef LS_BIND_BUTTON
	GetWorld()->GetTimerManager().SetTimer(StatusTimer, this, &ThisClass::RefreshState, 0.2f, true);
	RefreshPresets();
	RefreshState();
}

void ULSAutoBattleWidget::SetupBattlefieldPicker()
{
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>() : nullptr;
	if (!Mode || !WidgetTree)
		return;

	UWidget* EditorTitle = GetWidgetFromName(TEXT("EditorTitle"));
	auto* Canvas = EditorTitle ? Cast<UCanvasPanel>(EditorTitle->GetParent()) : nullptr;
	const bool bHasEditorPanel = Canvas != nullptr;
	if (!Canvas)
	{
		UWidget* Title = GetWidgetFromName(TEXT("UnitsTitle"));
		Canvas = Title ? Cast<UCanvasPanel>(Title->GetParent()) : nullptr;
	}
	if (!Canvas)
		return;

	auto* Picker = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("BattlefieldPicker"));
	Picker->SetToolTipText(FText::FromString(TEXT("전장 선택 · 변경 시 양쪽 편성이 초기화됩니다.")));
	auto* PickerSlot = Canvas->AddChildToCanvas(Picker);
	PickerSlot->SetPosition(bHasEditorPanel ? FVector2D(155, 17) : FVector2D(147, 190));
	PickerSlot->SetSize(bHasEditorPanel ? FVector2D(163, 34) : FVector2D(171, 29));
	if (auto* TitleSlot = bHasEditorPanel ? Cast<UCanvasPanelSlot>(EditorTitle->Slot) : nullptr)
		TitleSlot->SetSize(FVector2D(132, 36));
	for (const auto& Battlefield : Mode->GetBattlefieldOptions())
		Picker->AddOption(Battlefield.Label);
	Picker->OnSelectionChanged.AddDynamic(this, &ThisClass::OnBattlefieldSelected);
}

void ULSAutoBattleWidget::OnBattlefieldSelected(FString Selection, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct || SelectionType == ESelectInfo::OnNavigation)
		return;
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>() : nullptr;
	auto* Picker = Cast<UComboBoxString>(GetWidgetFromName(TEXT("BattlefieldPicker")));
	if (Mode && Picker && !Mode->SelectBattlefield(Picker->FindOptionIndex(Selection)))
		Picker->SetSelectedIndex(Mode->GetCurrentBattlefieldIndex());
}

void ULSAutoBattleWidget::EnsureTroopButtons()
{
	// 기존 개인 WBP의 병종 영역에 두 줄을 배치한다.
	UButton* SpearButton = Cast<UButton>(GetWidgetFromName(TEXT("Spear")));
	UCanvasPanel* Canvas = SpearButton ? Cast<UCanvasPanel>(SpearButton->GetParent()) : nullptr;
	if (!Canvas || !WidgetTree)
		return;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AutoBattleTroopButtons); ++Index)
	{
		const FName Name(AutoBattleTroopButtons[Index]);
		const FName LabelName(*FString::Printf(TEXT("%sLabel"), AutoBattleTroopButtons[Index]));
		UWidget* Button = GetWidgetFromName(Name);
		if (!Button)
		{
			auto* NewButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			auto* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			Label->SetJustification(ETextJustify::Center);
			NewButton->SetContent(Label);
			Canvas->AddChildToCanvas(NewButton);
			Button = NewButton;
		}
		if (UCanvasPanelSlot* ButtonSlot = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			const bool bFirstRow = Index < 4;
			ButtonSlot->SetPosition(bFirstRow ? FVector2D(18 + Index * 76, 230)
			                                  : FVector2D(18 + (Index - 4) * 102, 258));
			ButtonSlot->SetSize(FVector2D(bFirstRow ? 72 : 96, 26));
		}
		if (UTextBlock* Label = Cast<UTextBlock>(GetWidgetFromName(LabelName)))
		{
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = 13;
			Label->SetFont(Font);
		}
	}
}

void ULSAutoBattleWidget::SetLabel(const TCHAR* Name, const FString& Text)
{
	if (auto* Label = Cast<UTextBlock>(GetWidgetFromName(Name)))
	{
		Label->SetText(FText::FromString(Text));
	}
}

void ULSAutoBattleWidget::RefreshState()
{
	const auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	const auto* Controller = GetOwningPlayer<ALSAutoBattleController>();
	if (!Mode || !Controller)
	{
		return;
	}
	const ELSAutoBattlePhase Phase = Mode->GetPhase();
	const bool bPreparing = Phase == ELSAutoBattlePhase::Preparation;
	if (auto* Picker = Cast<UComboBoxString>(GetWidgetFromName(TEXT("BattlefieldPicker"))))
	{
		const int32 Index = Mode->GetCurrentBattlefieldIndex();
		Picker->SetIsEnabled(bPreparing && Index != INDEX_NONE);
		if (Index != INDEX_NONE && Picker->GetSelectedIndex() != Index)
			Picker->SetSelectedIndex(Index);
	}
	const bool bResult = Phase == ELSAutoBattlePhase::Victory || Phase == ELSAutoBattlePhase::Defeat;
	const bool bFighting = Phase == ELSAutoBattlePhase::Fighting;
	SetLabel(TEXT("Phase"), PhaseNames[static_cast<uint8>(Phase)]);
	SetLabel(TEXT("UnitsTitle"),
	         Controller->GetEditingFaction() == ELSFaction::Ally ? TEXT("1. 아군 병력 편성") : TEXT("1. 적 병력 편성"));
	if (UWidget* UnitsTitle = GetWidgetFromName(TEXT("UnitsTitle")))
		UnitsTitle->SetVisibility(bFighting ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	SetLabel(TEXT("Economy"), FString::Printf(TEXT("아군 병력 %d"), Mode->GetPopulation()));
	SetLabel(TEXT("AllyForcePopulation"), FText::AsNumber(Mode->GetPopulation()).ToString());
	SetLabel(TEXT("AllyForceCost"), FormationCost(Mode->GetFormation(), Mode->GetScenario()));
	SetLabel(TEXT("EnemyForcePopulation"),
	         Mode->GetScenario() ? FText::AsNumber(Mode->GetScenario()->EnemyFormation.Num()).ToString() : TEXT("—"));
	SetLabel(TEXT("EnemyForceCost"),
	         Mode->GetScenario() ? FormationCost(Mode->GetScenario()->EnemyFormation, Mode->GetScenario()) : TEXT("—"));
	SetLabel(TEXT("Health"), FString::Printf(TEXT("아군 성  %s\n적 성      %s"), *CastleHealth(Mode->GetAllyCastle()),
	                                         *CastleHealth(Mode->GetEnemyCastle())));
	const TOptional<ELSCastleWeapon> Armed = Controller->GetArmedWeapon();
	const ELSCastleWeapon PreparationWeapon = Controller->GetEditingFaction() == ELSFaction::Ally
	    ? Mode->GetWeapon() : Mode->GetScenario()->EnemyWeapon;
	FString WeaponStatus = FString::Printf(TEXT("선택 무장: %s"), WeaponNames[static_cast<uint8>(PreparationWeapon)]);
	if (bFighting)
	{
		WeaponStatus = Armed.IsSet()
		    ? FString::Printf(TEXT("조준 중: %s"), WeaponNames[static_cast<uint8>(Armed.GetValue())])
		    : TEXT("1 · 2 · 3으로 무장 선택");
		const auto CastResult = Controller->GetLastWeaponResult();
		if (CastResult == ELSWeaponCastResult::OutOfRange)
			WeaponStatus = TEXT("사거리 밖입니다. 지면을 다시 조준하세요.");
		else if (CastResult == ELSWeaponCastResult::InvalidAim)
			WeaponStatus = TEXT("유효한 지면을 조준하세요.");
		else if (CastResult == ELSWeaponCastResult::Cooldown)
			WeaponStatus = TEXT("무장 재사용 대기 중");
	}
	SetLabel(TEXT("Weapon"), WeaponStatus);
	SetLabel(TEXT("Help"), bFighting
	    ? TEXT("W/S 전진·후진 · A/D 차체 회전\n1 대포 · 2 산탄포 · 3 발리스타\n좌클릭 발사 · 우클릭/Esc 조준 취소")
	    : TEXT("우클릭 유지: 회전 + WASD / Q·E 상하\n가운데 드래그: 화면 이동\n휠: 앞뒤 · 우클릭+휠: 속도\nShift 가속 · Home 복귀"));
	const TCHAR* WeaponButtons[] = {TEXT("Cannon"), TEXT("Shotgun"), TEXT("Ballista")};
	const TCHAR* WeaponButtonNames[] = {TEXT("일반 대포"), TEXT("산탄포"), TEXT("발리스타")};
	const TCHAR* WeaponKeys[] = {TEXT("1"), TEXT("2"), TEXT("3")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AutoBattleTroopButtons); ++Index)
	{
		const int32* Cost =
		    Mode->GetScenario() ? Mode->GetScenario()->UnitCosts.Find(static_cast<ELSTroopType>(Index)) : nullptr;
		SetLabel(*FString::Printf(TEXT("%sLabel"), AutoBattleTroopButtons[Index]),
		         FString::Printf(TEXT("%s  %d"), AutoBattleTroopLabels[Index], Cost ? *Cost : 0));
		if (auto* Button = Cast<UButton>(GetWidgetFromName(AutoBattleTroopButtons[Index])))
		{
			Button->SetIsEnabled(bPreparing);
			Button->SetVisibility(bFighting ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
			Button->SetBackgroundColor(!Controller->IsErasing() && !Controller->IsChoosingCastle() &&
			                                   Controller->GetSelectionId() == 0 &&
			                                   static_cast<uint8>(Controller->GetChosenTroop()) == Index
			                               ? FLinearColor(0.12f, 0.65f, 0.9f)
			                               : FLinearColor(0.15f, 0.22f, 0.3f));
		}
		if (auto* Button = Cast<UButton>(Index < UE_ARRAY_COUNT(WeaponButtons) ? GetWidgetFromName(WeaponButtons[Index]) : nullptr))
		{
			const auto Weapon = static_cast<ELSCastleWeapon>(Index);
			const auto* Castle = Mode->GetAllyCastle();
			const float Remaining = bFighting && IsValid(Castle) ? Castle->Weapons->GetRemainingCooldown(Weapon) : 0;
			const FString ButtonLabel = bFighting
			    ? (Remaining > 0
			        ? FString::Printf(TEXT("%s · %.0f초"), WeaponKeys[Index], FMath::CeilToFloat(Remaining))
			        : FString::Printf(TEXT("%s %s"), WeaponKeys[Index], WeaponButtonNames[Index]))
			    : WeaponButtonNames[Index];
			SetLabel(*FString::Printf(TEXT("%sLabel"), WeaponButtons[Index]), ButtonLabel);
			Button->SetIsEnabled(bPreparing || (bFighting && Remaining <= 0));
			const bool bSelected = bFighting ? (Armed.IsSet() && Armed.GetValue() == Weapon)
			                                  : PreparationWeapon == Weapon;
			Button->SetBackgroundColor(bSelected
			                               ? FLinearColor(0.15f, 0.65f, 0.45f)
			                               : FLinearColor(0.15f, 0.22f, 0.3f));
		}
	}
	for (const TCHAR* Name : {TEXT("Castle"), TEXT("Reset"), TEXT("Remove"), TEXT("Start")})
	{
		if (UWidget* Button = GetWidgetFromName(Name))
		{
			Button->SetIsEnabled(bPreparing);
		}
	}
	SetLabel(TEXT("EditorHelp"), TEXT("병종 선택 → 좌클릭 드래그 생성\nDelete → 드래그 삭제\nShift+클릭 선택 → 지면 "
	                                  "클릭 이동\n아군은 코스트·인구 상한 없이 배치"));
	SetLabel(TEXT("RemoveLabel"), Controller->IsErasing() ? TEXT("삭제 브러시 ON") : TEXT("삭제 브러시 (Del)"));
	if (auto* Button = Cast<UButton>(GetWidgetFromName(TEXT("Remove"))))
	{
		Button->SetBackgroundColor(Controller->IsErasing() ? FLinearColor(0.8f, 0.2f, 0.12f)
		                                                   : FLinearColor(0.15f, 0.22f, 0.3f));
	}
	if (UWidget* Button = GetWidgetFromName(TEXT("Retry")))
	{
		Button->SetIsEnabled(bResult || Phase == ELSAutoBattlePhase::Fighting);
	}
	FString Message;
	if (Phase == ELSAutoBattlePhase::Error)
	{
		Message = Mode->GetFailure();
	}
	else if (Controller->GetLastRequest() != ELSAutoBattleRequest::Success)
	{
		const TCHAR* Reasons[] = {TEXT(""),
		                          TEXT("지금은 준비 단계가 아닙니다."),
		                          TEXT("편성 또는 설정을 확인하세요."),
		                          TEXT("배치 구역·겹침·지면을 확인하세요."),
		                          TEXT("적 병력 배치 한도입니다."),
		                          TEXT("선택한 병력이 없습니다."),
		                          TEXT("요청을 처리할 수 없습니다.")};
		Message = Reasons[static_cast<uint8>(Controller->GetLastRequest())];
	}
	else if (bPreparing)
	{
		Message = Controller->IsErasing()
		              ? TEXT("삭제: 좌클릭 드래그\n병종 버튼: 생성 · Esc: 취소")
		              : (Controller->IsChoosingCastle()
		                     ? TEXT("빈 지면을 클릭해 성의 시작 위치를 정하세요.")
		                     : (Controller->GetSelectionId()
		                            ? TEXT("지면 클릭: 선택 병력 이동\n병종 버튼: 생성 모드 · Delete: 삭제 모드")
		                            : TEXT("생성: 병종 선택 후 좌클릭 드래그\n삭제: Delete → 좌클릭 드래그\n개별 선택: "
		                                   "Shift+클릭")));
	}
	else if (bResult)
	{
		Message = TEXT("편성을 복원한 뒤 배치와 무장을 바꿔 재도전할 수 있습니다.");
	}
	else if (Phase == ELSAutoBattlePhase::Fighting)
	{
		const TOptional<ELSCastleWeapon> EnemyAim = Mode->GetEnemyAimingWeapon();
		Message = EnemyAim.IsSet()
		    ? FString::Printf(TEXT("적 성 %s 발사 예고"), WeaponButtonNames[static_cast<uint8>(EnemyAim.GetValue())])
		    : TEXT("편성 복원으로 교전을 중단하고 준비로 돌아갈 수 있습니다.");
	}
	else
	{
		Message = TEXT("전장을 준비하는 중입니다.");
	}
	SetLabel(TEXT("Message"), Message);
	SetLabel(TEXT("TeamStatus"),
	         FString::Printf(TEXT("편집: %s / 적 %d기"),
	                         Controller->GetEditingFaction() == ELSFaction::Ally ? TEXT("아군") : TEXT("적"),
	                         Mode->GetScenario() ? Mode->GetScenario()->EnemyFormation.Num() : 0));
	SetLabel(TEXT("EditorMessage"), Controller->GetEditorMessage());
	for (const TCHAR* Name : {TEXT("EditAllies"), TEXT("EditEnemies"), TEXT("SaveNewPreset"), TEXT("OverwritePreset"),
	                          TEXT("LoadSelectedPreset"), TEXT("PresetName"), TEXT("PresetList")})
	{
		if (auto* Control = GetWidgetFromName(Name))
		{
			Control->SetIsEnabled(bPreparing);
		}
	}
	for (const TCHAR* Name : {TEXT("EditAllies"), TEXT("EditEnemies")})
	{
		if (auto* Button = Cast<UButton>(GetWidgetFromName(Name)))
		{
			Button->SetBackgroundColor((Controller->GetEditingFaction() == ELSFaction::Ally) ==
			                                   (FCString::Strcmp(Name, TEXT("EditAllies")) == 0)
			                               ? FLinearColor(0.1f, 0.6f, 0.8f)
			                               : FLinearColor(0.15f, 0.22f, 0.3f));
		}
	}

}

bool ULSAutoBattleWidget::IsPointerOverPanel() const
{
	const UWidget* Panel = GetWidgetFromName(TEXT("Panel"));
	const UWidget* EditorPanel = GetWidgetFromName(TEXT("EditorPanel"));
	const UWidget* Comparison = GetWidgetFromName(TEXT("ForceComparisonPanel"));
	return (Panel && Panel->IsHovered()) || (EditorPanel && EditorPanel->IsHovered()) ||
	       (Comparison && Comparison->IsHovered());
}

void ULSAutoBattleWidget::NativeDestruct()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(StatusTimer);
	}
	Super::NativeDestruct();
}

void ULSAutoBattleWidget::Spear()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseTroop(ELSTroopType::Spearman);
	}
}
void ULSAutoBattleWidget::Archer()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseTroop(ELSTroopType::Archer);
	}
}
void ULSAutoBattleWidget::Cavalry()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseTroop(ELSTroopType::Cavalry);
	}
}

void ULSAutoBattleWidget::Medic()
{
	if (auto* Controller = Cast<ALSAutoBattleController>(GetOwningPlayer()))
		Controller->ChooseTroop(ELSTroopType::Medic);
}
void ULSAutoBattleWidget::Shieldbearer()
{
	if (auto* Controller = Cast<ALSAutoBattleController>(GetOwningPlayer()))
		Controller->ChooseTroop(ELSTroopType::Shieldbearer);
}
void ULSAutoBattleWidget::Mortar()
{
	if (auto* Controller = Cast<ALSAutoBattleController>(GetOwningPlayer()))
		Controller->ChooseTroop(ELSTroopType::Mortar);
}
void ULSAutoBattleWidget::Bomber()
{
	if (auto* Controller = Cast<ALSAutoBattleController>(GetOwningPlayer()))
		Controller->ChooseTroop(ELSTroopType::Bomber);
}
void ULSAutoBattleWidget::Castle()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseCastle();
	}
}
void ULSAutoBattleWidget::Cannon()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseWeapon(ELSCastleWeapon::Cannon);
	}
}
void ULSAutoBattleWidget::Shotgun()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseWeapon(ELSCastleWeapon::Shotgun);
	}
}
void ULSAutoBattleWidget::Ballista()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ChooseWeapon(ELSCastleWeapon::Ballista);
	}
}
void ULSAutoBattleWidget::Start()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->BeginBattle();
	}
}
void ULSAutoBattleWidget::Retry()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->Retry();
	}
}
void ULSAutoBattleWidget::Reset()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ClearFormation();
	}
}
void ULSAutoBattleWidget::Remove()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->ToggleEraseBrush();
	}
}

bool ULSAutoBattleWidget::IsEditingText() const
{
	for (const TCHAR* Name : {TEXT("PresetName"), TEXT("PresetList")})
	{
		if (auto* W = GetWidgetFromName(Name))
		{
			if (W->HasKeyboardFocus() || W->HasFocusedDescendants())
			{
				return true;
			}
		}
	}
	return false;
}
void ULSAutoBattleWidget::RefreshPresets()
{
	if (auto* List = Cast<UComboBoxString>(GetWidgetFromName(TEXT("PresetList"))))
	{
		const FString Old = List->GetSelectedOption();
		List->ClearOptions();
		for (const auto& Name : FLSAutoBattlePresetStore::List())
		{
			List->AddOption(Name);
		}
		if (List->FindOptionIndex(Old) != INDEX_NONE)
		{
			List->SetSelectedOption(Old);
		}
		else if (List->GetOptionCount() > 0)
		{
			List->SetSelectedIndex(0);
		}
	}
}
void ULSAutoBattleWidget::EditAllies()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->SetEditingFaction(ELSFaction::Ally);
	}
}
void ULSAutoBattleWidget::EditEnemies()
{
	if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
	{
		C->SetEditingFaction(ELSFaction::Enemy);
	}
}
void ULSAutoBattleWidget::SaveNewPreset()
{
	if (auto* Name = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("PresetName"))))
	{
		if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
		{
			C->SavePreset(Name->GetText().ToString(), false);
		}
	}
}
void ULSAutoBattleWidget::OverwritePreset()
{
	if (auto* Name = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("PresetName"))))
	{
		if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
		{
			C->SavePreset(Name->GetText().ToString(), true);
		}
	}
}
void ULSAutoBattleWidget::LoadSelectedPreset()
{
	if (auto* List = Cast<UComboBoxString>(GetWidgetFromName(TEXT("PresetList"))))
	{
		if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
		{
			C->LoadPreset(List->GetSelectedOption());
		}
	}
}
void ULSAutoBattleWidget::ReloadPresetList()
{
	RefreshPresets();
}

void ULSAutoBattleWidget::SelectPreset(const FString& Name)
{
	if (auto* List = Cast<UComboBoxString>(GetWidgetFromName(TEXT("PresetList"))))
	{
		List->SetSelectedOption(Name);
	}
	if (auto* Input = Cast<UEditableTextBox>(GetWidgetFromName(TEXT("PresetName"))))
	{
		Input->SetText(FText::FromString(Name));
	}
}
