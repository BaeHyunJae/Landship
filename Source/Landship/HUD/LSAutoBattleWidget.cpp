#include "HUD/LSAutoBattleWidget.h"
#include "Controls/LSAutoBattleController.h"
#include "Match/LSAutoBattleGameMode.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Match/LSAutoBattlePreset.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
const TCHAR* PhaseNames[] = {TEXT("전장 준비 중"), TEXT("전투 준비"), TEXT("자동 전투 관전"),
                             TEXT("승리"),         TEXT("패배"),      TEXT("설정 오류")};
const TCHAR* WeaponNames[] = {TEXT("일반 대포 · 밀집한 적"), TEXT("산탄포 · 가까운 적"),
                              TEXT("발리스타 · 체력이 높은 적")};
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
FString CastleHealth(const ALSMobileCastle* Castle)
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
	LS_BIND_BUTTON("ApplyLimits", ApplyLimits)
	LS_BIND_BUTTON("SaveNewPreset", SaveNewPreset)
	LS_BIND_BUTTON("OverwritePreset", OverwritePreset)
	LS_BIND_BUTTON("LoadSelectedPreset", LoadSelectedPreset)
	LS_BIND_BUTTON("ReloadPresetList", ReloadPresetList)
#undef LS_BIND_BUTTON
	GetWorld()->GetTimerManager().SetTimer(StatusTimer, this, &ThisClass::RefreshState, 0.2f, true);
	RefreshPresets();
	RefreshState();
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
	const bool bResult = Phase == ELSAutoBattlePhase::Victory || Phase == ELSAutoBattlePhase::Defeat;
	SetLabel(TEXT("Phase"), PhaseNames[static_cast<uint8>(Phase)]);
	SetLabel(TEXT("UnitsTitle"),
	         Controller->GetEditingFaction() == ELSFaction::Ally ? TEXT("1. 아군 병력 편성") : TEXT("1. 적 병력 편성"));
	SetLabel(TEXT("Economy"), FString::Printf(TEXT("예산 %d    병력 %d / %d"), Mode->GetBudget(), Mode->GetPopulation(),
	                                          Mode->GetScenario() ? Mode->GetScenario()->PopulationLimit : 0));
	SetLabel(TEXT("AllyForcePopulation"), FText::AsNumber(Mode->GetPopulation()).ToString());
	SetLabel(TEXT("AllyForceCost"), FormationCost(Mode->GetFormation(), Mode->GetScenario()));
	SetLabel(TEXT("EnemyForcePopulation"),
	         Mode->GetScenario() ? FText::AsNumber(Mode->GetScenario()->EnemyFormation.Num()).ToString() : TEXT("—"));
	SetLabel(TEXT("EnemyForceCost"),
	         Mode->GetScenario() ? FormationCost(Mode->GetScenario()->EnemyFormation, Mode->GetScenario()) : TEXT("—"));
	SetLabel(TEXT("Health"), FString::Printf(TEXT("아군 성  %s\n적 성      %s"), *CastleHealth(Mode->GetAllyCastle()),
	                                         *CastleHealth(Mode->GetEnemyCastle())));
	SetLabel(TEXT("Weapon"),
	         FString::Printf(TEXT("선택 무장: %s"),
	                         WeaponNames[static_cast<uint8>(Controller->GetEditingFaction() == ELSFaction::Ally
	                                                            ? Mode->GetWeapon()
	                                                            : Mode->GetScenario()->EnemyWeapon)]));
	const TCHAR* TroopNames[] = {TEXT("창병"), TEXT("궁병"), TEXT("기마병")};
	const TCHAR* TroopButtons[] = {TEXT("Spear"), TEXT("Archer"), TEXT("Cavalry")};
	const TCHAR* WeaponButtons[] = {TEXT("Cannon"), TEXT("Shotgun"), TEXT("Ballista")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const int32* Cost =
		    Mode->GetScenario() ? Mode->GetScenario()->UnitCosts.Find(static_cast<ELSTroopType>(Index)) : nullptr;
		SetLabel(*FString::Printf(TEXT("%sLabel"), TroopButtons[Index]),
		         FString::Printf(TEXT("%s  %d"), TroopNames[Index], Cost ? *Cost : 0));
		if (auto* Button = Cast<UButton>(GetWidgetFromName(TroopButtons[Index])))
		{
			Button->SetIsEnabled(bPreparing);
			Button->SetBackgroundColor(!Controller->IsErasing() && !Controller->IsChoosingCastle() &&
			                                   Controller->GetSelectionId() == 0 &&
			                                   static_cast<uint8>(Controller->GetChosenTroop()) == Index
			                               ? FLinearColor(0.12f, 0.65f, 0.9f)
			                               : FLinearColor(0.15f, 0.22f, 0.3f));
		}
		if (auto* Button = Cast<UButton>(GetWidgetFromName(WeaponButtons[Index])))
		{
			Button->SetIsEnabled(bPreparing);
			Button->SetBackgroundColor(static_cast<uint8>(Controller->GetEditingFaction() == ELSFaction::Ally
			                                                  ? Mode->GetWeapon()
			                                                  : Mode->GetScenario()->EnemyWeapon) == Index
			                               ? FLinearColor(0.15f, 0.65f, 0.45f)
			                               : FLinearColor(0.15f, 0.22f, 0.3f));
		}
	}
	for (const TCHAR* Name : {TEXT("Castle"), TEXT("Reset"), TEXT("Remove"), TEXT("Start")})
	{
		if (UWidget* Button = GetWidgetFromName(Name))
		{
			Button->SetIsEnabled(bPreparing &&
			                     (FCString::Strcmp(Name, TEXT("Start")) != 0 || Mode->GetPopulation() > 0));
		}
	}
	SetLabel(TEXT("EditorHelp"), TEXT("병종 선택 → 좌클릭 드래그 생성\nDelete → 드래그 삭제\nShift+클릭 선택 → 지면 "
	                                  "클릭 이동\n적은 예산·인구 제한 없이 배치"));
	SetLabel(TEXT("RemoveLabel"), Controller->IsErasing() ? TEXT("삭제 브러시 ON") : TEXT("삭제 브러시 (Del)"));
	if (auto* Button = Cast<UButton>(GetWidgetFromName(TEXT("Remove"))))
	{
		Button->SetBackgroundColor(Controller->IsErasing() ? FLinearColor(0.8f, 0.2f, 0.12f)
		                                                   : FLinearColor(0.15f, 0.22f, 0.3f));
	}
	if (UWidget* Button = GetWidgetFromName(TEXT("Retry")))
	{
		Button->SetIsEnabled(bResult);
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
		                          TEXT("예산이 부족합니다."),
		                          TEXT("인구수 한도입니다."),
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
	else
	{
		Message = TEXT("전투 중 병력·무장 조작은 불가능합니다.");
	}
	SetLabel(TEXT("Message"), Message);
	SetLabel(TEXT("TeamStatus"),
	         FString::Printf(TEXT("편집: %s / 적 %d기"),
	                         Controller->GetEditingFaction() == ELSFaction::Ally ? TEXT("아군") : TEXT("적"),
	                         Mode->GetScenario() ? Mode->GetScenario()->EnemyFormation.Num() : 0));
	SetLabel(TEXT("EditorMessage"), Controller->GetEditorMessage());
	for (const TCHAR* Name : {TEXT("EditAllies"), TEXT("EditEnemies"), TEXT("ApplyLimits"), TEXT("BudgetInput"),
	                          TEXT("PopulationInput"), TEXT("SaveNewPreset"), TEXT("OverwritePreset"),
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
	if (Mode->GetScenario() && DisplayedBattleId != Mode->GetBattleId())
	{
		if (auto* Input = Cast<USpinBox>(GetWidgetFromName(TEXT("BudgetInput"))))
		{
			Input->SetValue(Mode->GetScenario()->Budget);
		}
		if (auto* Input = Cast<USpinBox>(GetWidgetFromName(TEXT("PopulationInput"))))
		{
			Input->SetValue(Mode->GetScenario()->PopulationLimit);
		}
		DisplayedBattleId = Mode->GetBattleId();
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
	for (const TCHAR* Name : {TEXT("PresetName"), TEXT("BudgetInput"), TEXT("PopulationInput"), TEXT("PresetList")})
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
void ULSAutoBattleWidget::ApplyLimits()
{
	auto* Budget = Cast<USpinBox>(GetWidgetFromName(TEXT("BudgetInput")));
	auto* Limit = Cast<USpinBox>(GetWidgetFromName(TEXT("PopulationInput")));
	if (Budget && Limit)
	{
		if (auto* C = GetOwningPlayer<ALSAutoBattleController>())
		{
			C->SetLimits(FMath::RoundToInt(Budget->GetValue()), FMath::RoundToInt(Limit->GetValue()));
		}
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
