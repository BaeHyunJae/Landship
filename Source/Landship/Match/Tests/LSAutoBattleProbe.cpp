#include "Match/Tests/LSAutoBattleProbe.h"
#include "Match/LSAutoBattlePreset.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Troops/LSCavalryComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Match/LSAutoBattleGameMode.h"
#include "Controls/LSAutoBattleController.h"
#include "Controls/LSFreeCameraPawn.h"
#include "Controls/LSOrderComponent.h"
#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "HUD/LSAutoBattleWidget.h"
#include "Components/Button.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

ALSAutoBattleProbe::ALSAutoBattleProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSAutoBattleProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("TABSAutoBattleTestExit")) &&
	    !FParse::Param(FCommandLine::Get(), TEXT("TABSMatchupTestExit")) &&
	    !FParse::Param(FCommandLine::Get(), TEXT("TABSScenarioEditorTestExit")) &&
	    !FParse::Param(FCommandLine::Get(), TEXT("TABSExpandedMapTestExit")) &&
	    !FParse::Param(FCommandLine::Get(), TEXT("TABSBrushTestExit")) &&
	    !FParse::Param(FCommandLine::Get(), TEXT("TABSDenseRetryTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}

bool ALSAutoBattleProbe::HasFullHealth(AActor* Actor) const
{
	FLSHealthSnapshot Snapshot;
	const auto* Component = IsValid(Actor) ? Actor->FindComponentByClass<ULSHealthComponent>() : nullptr;
	return Component && Component->TryGetHealth(Snapshot) && !Snapshot.bIsDead &&
	       Snapshot.CurrentHealth == Snapshot.MaxHealth;
}
void ALSAutoBattleProbe::Check(bool bCondition, const TCHAR* Label)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("TABS_PROBE %s: %s"), bCondition ? TEXT("PASS") : TEXT("FAIL"), Label);
}
void ALSAutoBattleProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("TABS_PROBE_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSAutoBattleProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSDenseRetryTestExit")))
	{
		TickDenseRetry(DeltaSeconds);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSBrushTestExit")))
	{
		TickPlacementBrush(DeltaSeconds);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSExpandedMapTestExit")))
	{
		TickExpandedBattlefield(DeltaSeconds);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSScenarioEditorTestExit")))
	{
		TickScenarioEditor(DeltaSeconds);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSMatchupTestExit")))
	{
		TickMatchup(DeltaSeconds);
		return;
	}
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 110 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("전용 모드 초기화 또는 시나리오 제한 시간"));
		Finish();
		return;
	}
	ALSAutoBattleController* Controller = Cast<ALSAutoBattleController>(GetWorld()->GetFirstPlayerController());
	if (Stage == 0 && Mode->GetPhase() == ELSAutoBattlePhase::Preparation && StageTime > 1)
	{
		Check(Controller && Cast<ALSFreeCameraPawn>(Controller->GetPawn()), TEXT("자유 1인칭 카메라와 전용 컨트롤러"));
		Check(Mode->StartBattle() == ELSAutoBattleRequest::InvalidData, TEXT("빈 편성으로 전투 시작 거부"));
		int32 Id = 77;
		const int32 Budget = Mode->GetBudget();
		Check(Mode->PlaceUnit(ELSTroopType::Spearman, FVector(1000, 1000, 0), Id) ==
		              ELSAutoBattleRequest::InvalidPlacement &&
		          Mode->GetBudget() == Budget && Id == 77,
		      TEXT("배치 구역 밖 실패 시 예산과 출력 보존"));
		for (int32 Index = 0; Index < 10; ++Index)
		{
			Id = 0;
			Check(Mode->PlaceUnit(static_cast<ELSTroopType>(Index % 3),
			                      FVector(-1100 + (Index / 5) * 420, -1050 + (Index % 5) * 440, 0),
			                      Id) == ELSAutoBattleRequest::Success,
			      TEXT("세 병종의 준비 배치 성공"));
			Ids.Add(Id);
		}
		if (!Mode->GetPlacementActor(Ids[0]))
		{
			Check(false, TEXT("편성 액터 생성"));
			Finish();
			return;
		}
		FirstUnit = Mode->GetPlacementActor(Ids[0]);
		BudgetAfterPlacement = Mode->GetBudget();
		Check(!FirstUnit->Orders->IsComponentTickEnabled() && !FirstUnit->BasicAttack->IsActive(),
		      TEXT("준비 단계 RTS 명령과 공격 비활성"));
		Check(Mode->PlaceUnit(ELSTroopType::Cavalry, FVector(-1100, -1050, 0), Id) ==
		              ELSAutoBattleRequest::InvalidPlacement &&
		          Mode->GetBudget() == BudgetAfterPlacement,
		      TEXT("병력 겹침 배치 실패 시 비용 보존"));
		Check(Mode->MoveUnit(Ids[0], FVector(-1100, -1200, 0)) == ELSAutoBattleRequest::Success,
		      TEXT("기존 병력 위치 수정"));
		Check(Mode->MoveCastle(FVector(1500, 0, 0)) == ELSAutoBattleRequest::InvalidPlacement,
		      TEXT("성의 적 구역 배치 거부"));
		Check(Mode->SelectWeapon(ELSCastleWeapon::Ballista) == ELSAutoBattleRequest::Success,
		      TEXT("준비 단계 성 무장 선택"));
		Check(Mode->GetAllyCastle()->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector::ZeroVector) ==
		          ELSWeaponCastResult::Unavailable,
		      TEXT("준비 중 공통 무장 시전 경로도 차단"));
		if (Controller && Controller->GetBattleWidget())
		{
			auto* Button = Cast<UButton>(Controller->GetBattleWidget()->GetWidgetFromName(TEXT("Start")));
			Check(Button && Button->GetIsEnabled(), TEXT("실제 WBP 전투 시작 버튼 활성"));
		}
		else
		{
			Check(false, TEXT("전용 WBP 생성"));
		}

		if (auto* Camera = Controller ? Cast<ALSFreeCameraPawn>(Controller->GetPawn()) : nullptr)
		{
			const FVector CastleLocation = Mode->GetAllyCastle()->GetActorLocation();
			Camera->SetActorLocation(CastleLocation + FVector(400, 0, 0));
			Camera->SetActorRotation(FRotator(0, 180, 0));
			Camera->MoveCamera(FVector::ForwardVector, 0.5f, false);
			Check(FVector::Dist2D(Camera->GetActorLocation(), CastleLocation) >= 160 &&
			          FVector::Dist2D(Camera->GetActorLocation(), CastleLocation) < 390,
			      TEXT("관전 카메라는 성 외곽에서 충돌"));
			Camera->SetActorLocation(FirstUnit->GetActorLocation() + FVector(300, 0, 0));
			const FVector CameraStart = Camera->GetActorLocation();
			Camera->MoveCamera(FVector::ForwardVector, 0.5f, false);
			Check(FVector::Dist(CameraStart, Camera->GetActorLocation()) > 490, TEXT("관전 카메라는 일반 병력을 통과"));
			Camera->ResetView();
		}
		InitialBattleId = Mode->GetBattleId();
		if (Controller)
		{
			Controller->ConsoleCommand(TEXT("Shot ShowUI"));
		}
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 1)
	{
		Check(HasFullHealth(FirstUnit.Get()) && HasFullHealth(Mode->GetAllyCastle()),
		      TEXT("준비 중 체력과 전투 상태 보존"));
		if (Controller && Controller->GetBattleWidget())
		{
			if (auto* Button = Cast<UButton>(Controller->GetBattleWidget()->GetWidgetFromName(TEXT("Start"))))
			{
				Button->OnClicked.Broadcast();
			}
		}
		Check(Mode->GetPhase() == ELSAutoBattlePhase::Fighting, TEXT("실제 WBP 버튼으로 준비에서 자동 전투 전환"));
		Check(Mode->StartBattle() == ELSAutoBattleRequest::WrongPhase, TEXT("전투 시작 중복 요청 거부"));
		Check(Mode->MoveUnit(Ids[0], FVector(-1000, -900, 0)) == ELSAutoBattleRequest::WrongPhase &&
		          Mode->RemoveUnit(Ids[0]) == ELSAutoBattleRequest::WrongPhase &&
		          Mode->ResetFormation() == ELSAutoBattleRequest::WrongPhase &&
		          Mode->SelectWeapon(ELSCastleWeapon::Cannon) == ELSAutoBattleRequest::WrongPhase &&
		          Mode->RetryBattle() == ELSAutoBattleRequest::WrongPhase,
		      TEXT("전투 중 배치·제거·무장·재도전 요청 차단"));
		Check(Controller && !Controller->IsCameraCaptured(), TEXT("전투 시작이 마우스를 자동 캡처하지 않음"));
		if (Controller)
		{
			Controller->LookPressed();
			Check(Controller->IsCameraCaptured(), TEXT("전투 중 우클릭 누름으로 관전 시작"));
			Controller->LookReleased();
			Check(!Controller->IsCameraCaptured(), TEXT("전투 중에도 우클릭을 놓으면 커서 복원"));
			Controller->PanPressed();
			Check(Controller->IsCameraCaptured(), TEXT("가운데 버튼 누름으로 화면 이동 시작"));
			Controller->PanReleased();
			Check(!Controller->IsCameraCaptured(), TEXT("가운데 버튼 해제로 커서 복원"));
		}
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 2)
	{
		Check(FirstUnit.IsValid() && FirstUnit->GetActorLocation().X > -1050, TEXT("직접 명령 없이 병력 자동 진격"));
		if (FirstUnit.IsValid())
		{
			Check(FirstUnit->Orders->ApplyKnockback(FVector(-200, 0, 200)), TEXT("자동 전투 중 공통 넉백 적용"));
		}
		if (Controller)
		{
			Controller->ConsoleCommand(TEXT("Shot ShowUI"));
		}
		Stage = 3;
		StageTime = 0;
	}
	else if (Stage == 3 && StageTime > 2)
	{
		Check(!FirstUnit.IsValid() || !FirstUnit->Orders->IsKnockedBack(), TEXT("착지 후 자동 전투의 넉백 상태 복구"));
		if (FParse::Param(FCommandLine::Get(), TEXT("TABSFastRetryTest")))
		{
			for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
			{
				if (It->GetFaction() == ELSFaction::Enemy)
				{
					It->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
					    ULSHealthComponent::MakeDamageRequest(100000, nullptr, nullptr));
				}
			}
		}
		Stage = 4;
		StageTime = 0;
	}
	else if (Stage == 4 &&
	         (Mode->GetPhase() == ELSAutoBattlePhase::Victory || Mode->GetPhase() == ELSAutoBattlePhase::Defeat))
	{
		Check(true, TEXT("플레이어 개입 없이 전투 결과 확정"));
		if (Controller)
		{
			Controller->ConsoleCommand(TEXT("Shot ShowUI"));
		}
		Stage = 5;
		StageTime = 0;
	}
	else if (Stage == 5 && StageTime > 1)
	{
		Check(Mode->RetryBattle() == ELSAutoBattleRequest::Success, TEXT("결과에서 직전 편성으로 재도전 준비"));
		Check(Mode->GetPhase() == ELSAutoBattlePhase::Loading &&
		          Mode->RetryBattle() == ELSAutoBattleRequest::WrongPhase,
		      TEXT("재도전 복원 중 중복 요청 거부"));
		Stage = 7;
		StageTime = 0;
	}
	else if (Stage == 7 && Mode->GetPhase() == ELSAutoBattlePhase::Preparation)
	{
		Check(Mode->GetBattleId() == InitialBattleId + 1 && Mode->GetPopulation() == 10 &&
		          Mode->GetBudget() == BudgetAfterPlacement && Mode->GetPlacementActor(Ids[0]) != FirstUnit.Get(),
		      TEXT("전투 식별자 갱신·편성 경제 복원·액터 재생성"));
		Check(HasFullHealth(Mode->GetAllyCastle()) && Controller && !Controller->IsCameraCaptured(),
		      TEXT("성 내구도와 준비 커서 복원"));
		Check(Mode->RemoveUnit(Ids[0]) == ELSAutoBattleRequest::Success && Mode->GetBudget() > BudgetAfterPlacement,
		      TEXT("병력 제거와 비용 환급"));
		Mode->StartBattle();
		// 같은 프레임의 전멸과 성 사망은 패배를 우선한다.
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			if (It->GetFaction() == ELSFaction::Enemy || *It == Mode->GetAllyCastle())
			{
				It->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				    ULSHealthComponent::MakeDamageRequest(100000, nullptr, nullptr));
			}
		}
		Stage = 6;
		StageTime = 0;
	}
	else if (Stage == 6 && StageTime > 0.2)
	{
		Check(Mode->GetPhase() == ELSAutoBattlePhase::Defeat, TEXT("같은 프레임 적 전멸과 아군 성 사망은 패배"));
		Check(Mode->RetryBattle(false) == ELSAutoBattleRequest::Success, TEXT("빈 편성 복원 요청 접수"));
		Stage = 8;
		StageTime = 0;
	}
	else if (Stage == 8 && Mode->GetPhase() == ELSAutoBattlePhase::Preparation)
	{
		Check(Mode->GetPopulation() == 0 && Mode->GetBudget() == Mode->GetScenario()->Budget,
		      TEXT("빈 편성으로 재도전 준비와 예산 복원"));
		Finish();
	}
}

void ALSAutoBattleProbe::TickMatchup(float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 100 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("상성 실전 검사 초기화/시간 제한"));
		Finish();
		return;
	}
	const ELSTroopType AllyTypes[] = {ELSTroopType::Archer, ELSTroopType::Spearman, ELSTroopType::Cavalry};
	const ELSTroopType EnemyTypes[] = {ELSTroopType::Spearman, ELSTroopType::Cavalry, ELSTroopType::Archer};
	if (Mode->GetPhase() == ELSAutoBattlePhase::Preparation && !bMatchupRunning && StageTime > 0.5f)
	{
		// 첫 준비 편성은 이미 생성되었으므로 명시적으로 다음 복원에서 검사 편성으로 바꾼다.
		if (!bMatchupWaiting)
		{
			Mode->GetScenario()->EnemyFormation.Reset();
			FLSAutoBattlePlacement Placement;
			Placement.Type = EnemyTypes[MatchupRound];
			Placement.Position = FVector(800, 0, 0);
			Mode->GetScenario()->EnemyFormation.Add(Placement);
			int32 Id = 0;
			Mode->PlaceUnit(AllyTypes[MatchupRound], FVector(-800, 0, 0), Id);
			Mode->StartBattle();
			for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
			{
				if (It->GetFaction() == ELSFaction::Enemy)
				{
					It->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
					    ULSHealthComponent::MakeDamageRequest(10000, Mode->GetAllyCastle(), Mode->GetAllyCastle()));
				}
			}
			bMatchupWaiting = true;
			return;
		}
		int32 Id = 0;
		Check(Mode->PlaceUnit(AllyTypes[MatchupRound], FVector(-800, 0, 0), Id) == ELSAutoBattleRequest::Success,
		      TEXT("상성 검사 아군 배치"));
		MatchupAlly = Mode->GetPlacementActor(Id);
		for (TActorIterator<ALSTroopUnit> It(GetWorld()); It; ++It)
		{
			if (ULSCombatTargetLibrary::IsCombatAlive(*It) && It->GetFaction() == ELSFaction::Enemy)
			{
				MatchupEnemy = *It;
			}
		}
		Check(MatchupEnemy.IsValid(), TEXT("상성 검사 적 배치"));
		Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("실제 자동 전투 시작"));
		for (ALSAutoBattleCastle* Castle : {Mode->GetAllyCastle(), Mode->GetEnemyCastle()})
		{
			Castle->Weapons->Deactivate();
			Castle->GetCharacterMovement()->DisableMovement();
		}
		bMatchupRunning = true;
		StageTime = 0;
		return;
	}
	if (bMatchupRunning && (!ULSCombatTargetLibrary::IsCombatAlive(MatchupAlly.Get()) ||
	                        !ULSCombatTargetLibrary::IsCombatAlive(MatchupEnemy.Get())))
	{
		Check(ULSCombatTargetLibrary::IsCombatAlive(MatchupAlly.Get()), TEXT("동수 일대일 상성 방향 승리"));
		ALSTroopUnit* Cavalry = Cast<ALSTroopUnit>((MatchupRound == 1 ? MatchupEnemy : MatchupAlly).Get());
		if (MatchupRound > 0 && Cavalry)
		{
			Check(Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->GetImpactCount() == 1,
			      TEXT("실제 주행 돌격 한 번 명중"));
			Check(Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->WasLastImpactGuarded() == (MatchupRound == 1),
			      TEXT("창병 정면 대비만 돌격 감소"));
		}
		UE_LOG(LogTemp, Display, TEXT("TABS_MATCHUP round=%d allyAlive=%d seconds=%.2f"), MatchupRound,
		       ULSCombatTargetLibrary::IsCombatAlive(MatchupAlly.Get()), StageTime);
		bMatchupRunning = false;
		++MatchupRound;
		if (MatchupRound == 3)
		{
			Finish();
			return;
		}
		Mode->GetScenario()->EnemyFormation[0].Type = EnemyTypes[MatchupRound];
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			if (It->GetFaction() == ELSFaction::Enemy && ULSCombatTargetLibrary::IsCombatAlive(*It))
			{
				It->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				    ULSHealthComponent::MakeDamageRequest(10000, Mode->GetAllyCastle(), Mode->GetAllyCastle()));
			}
		}
	}
	if (!bMatchupRunning && Mode->GetPhase() == ELSAutoBattlePhase::Victory)
	{
		Check(Mode->RetryBattle(false) == ELSAutoBattleRequest::Success, TEXT("다음 상성 검사 새 전투 복원"));
		StageTime = 0;
	}
}

void ALSAutoBattleProbe::TickScenarioEditor(float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 30 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("시나리오 편집 초기화"));
		Finish();
		return;
	}
	if (Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
	{
		return;
	}
	auto* Controller = Cast<ALSAutoBattleController>(GetWorld()->GetFirstPlayerController());
	auto* Widget = Controller ? Controller->GetBattleWidget() : nullptr;
	if (!Widget)
	{
		Check(false, TEXT("편집 위젯 생성"));
		Finish();
		return;
	}
	FString Name;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScenarioPresetName="), Name))
	{
		Name = TEXT("AutomationNativeScenario");
	}
	auto Click = [this, Widget](const TCHAR* ButtonName)
	{
		auto* B = Cast<UButton>(Widget->GetWidgetFromName(ButtonName));
		Check(B && B->GetIsEnabled(), ButtonName);
		if (B)
		{
			B->OnClicked.Broadcast();
		}
	};
	if (FParse::Param(FCommandLine::Get(), TEXT("TABSScenarioReadOnly")))
	{
		FString Error;
		Check(Mode->LoadPreset(Name, Error), TEXT("별도 프로세스 JSON 로드"));
		Check(Mode->GetPopulation() == 1 && Mode->GetScenario()->EnemyFormation.Num() == 3 &&
		          Mode->GetBudget() == 860 && Mode->GetScenario()->PopulationLimit == 20,
		      TEXT("새 프로세스 편성 경제 복원"));
		Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("새 프로세스 로드 후 전투 시작"));
		Finish();
		return;
	}
	auto SummaryEquals = [Widget](const TCHAR* Name, const TCHAR* Expected)
	{
		const auto* Label = Cast<UTextBlock>(Widget->GetWidgetFromName(Name));
		return Label && Label->GetText().ToString() == Expected;
	};
	int32 Id = 0;
	const int32 Budget = Mode->GetBudget();
	Click(TEXT("EditEnemies"));
	Check(Controller->GetEditingFaction() == ELSFaction::Enemy, TEXT("실제 버튼으로 적 편집 전환"));
	Mode->ResetFormation(ELSFaction::Enemy);
	for (int32 I = 0; I < 3; ++I)
	{
		Check(Mode->PlaceUnit(static_cast<ELSTroopType>(I), FVector(700, (I - 1) * 600, 0), Id, ELSFaction::Enemy) ==
		              ELSAutoBattleRequest::Success &&
		          Id < 0,
		      TEXT("적 병종 배치와 별도 ID"));
	}
	Check(Mode->GetBudget() == Budget && Mode->GetPopulation() == 0, TEXT("적 배치에 아군 자원·인구 소비 없음"));
	Check(SummaryEquals(TEXT("EnemyForcePopulation"), TEXT("3")) &&
	          SummaryEquals(TEXT("EnemyForceCost"), TEXT("190")) && SummaryEquals(TEXT("AllyForceCost"), TEXT("0")),
	      TEXT("적 편성 인구·명목 코스트 비교 표시"));
	Check(Mode->MoveUnit(Id, FVector(1000, 600, 0)) == ELSAutoBattleRequest::Success, TEXT("적 병력 이동"));
	Check(Mode->RemoveUnit(Id) == ELSAutoBattleRequest::Success && Mode->GetBudget() == Budget,
	      TEXT("적 제거로 아군 자원 늘지 않음"));
	Check(SummaryEquals(TEXT("EnemyForcePopulation"), TEXT("2")) && SummaryEquals(TEXT("EnemyForceCost"), TEXT("100")),
	      TEXT("적 삭제 즉시 비교 갱신"));
	Check(Mode->PlaceUnit(ELSTroopType::Cavalry, FVector(700, 600, 0), Id, ELSFaction::Enemy) ==
	          ELSAutoBattleRequest::Success,
	      TEXT("삭제 후 적 다시 배치"));
	Check(Mode->MoveCastle(FVector(1700, 1000, 0), ELSFaction::Enemy) == ELSAutoBattleRequest::Success,
	      TEXT("적 성 위치 수정"));
	Controller->ChooseWeapon(ELSCastleWeapon::Ballista);
	Check(Mode->GetScenario()->EnemyWeapon == ELSCastleWeapon::Ballista, TEXT("적 성 무장 수정"));
	Click(TEXT("EditAllies"));
	Check(Mode->PlaceUnit(ELSTroopType::Spearman, FVector(-800, 0, 0), Id) == ELSAutoBattleRequest::Success,
	      TEXT("아군 편성 배치"));
	auto* BudgetInput = Cast<USpinBox>(Widget->GetWidgetFromName(TEXT("BudgetInput")));
	auto* PopulationInput = Cast<USpinBox>(Widget->GetWidgetFromName(TEXT("PopulationInput")));
	if (!BudgetInput || !PopulationInput)
	{
		Check(false, TEXT("경제 입력 위젯"));
		Finish();
		return;
	}
	BudgetInput->SetValue(900);
	PopulationInput->SetValue(20);
	Click(TEXT("ApplyLimits"));
	Check(Mode->GetBudget() == 860 && Mode->GetScenario()->PopulationLimit == 20, TEXT("실제 버튼으로 자원·인구 조절"));
	auto* NameInput = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("PresetName")));
	if (!NameInput)
	{
		Check(false, TEXT("이름 입력 위젯"));
		Finish();
		return;
	}
	NameInput->SetText(FText::FromString(Name));
	Click(TEXT("SaveNewPreset"));
	Check(FLSAutoBattlePresetStore::List().Contains(Name) &&
	          Controller->GetEditorMessage().StartsWith(TEXT("저장 완료")),
	      TEXT("실제 저장 버튼으로 JSON 생성"));
	ALSUnit* OldUnit = Mode->GetPlacementActor(Id);
	Mode->ResetFormation(ELSFaction::Enemy);
	auto* List = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("PresetList")));
	if (!List)
	{
		Check(false, TEXT("프리셋 목록"));
		Finish();
		return;
	}
	List->SetSelectedOption(Name);
	Click(TEXT("LoadSelectedPreset"));
	Check(Mode->GetPopulation() == 1 && Mode->GetScenario()->EnemyFormation.Num() == 3 && Mode->GetBudget() == 860,
	      TEXT("양측 편성·예산 복원"));
	Check(Mode->GetPlacementActor(1) != OldUnit, TEXT("로드는 새 액터로 복원"));
	Check(SummaryEquals(TEXT("AllyForcePopulation"), TEXT("1")) && SummaryEquals(TEXT("AllyForceCost"), TEXT("40")) &&
	          SummaryEquals(TEXT("EnemyForcePopulation"), TEXT("3")) &&
	          SummaryEquals(TEXT("EnemyForceCost"), TEXT("190")),
	      TEXT("프리셋 로드 후 양측 편성 비교 복원"));
	auto Bad = Mode->GetPreset();
	const auto Duplicate = Bad.Enemies[0];
	Bad.Enemies.Add(Duplicate);
	ALSUnit* Before = Mode->GetPlacementActor(1);
	Check(Mode->ApplyPreset(Bad) == ELSAutoBattleRequest::InvalidPlacement && Mode->GetPlacementActor(1) == Before &&
	          ULSCombatTargetLibrary::IsCombatAlive(Before),
	      TEXT("겹친 프리셋은 기존 편성·액터 유지"));
	Check(Mode->SetPreparationLimits(0, 20) == ELSAutoBattleRequest::InvalidData && Mode->GetBudget() == 860,
	      TEXT("사용한 비용 아래로 예산 축소 거부"));
	Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("로드한 편성 전투 시작"));
	FString Error;
	Check(!Mode->SavePreset(Name, true, Error) && !Mode->LoadPreset(Name, Error), TEXT("전투 중 저장·불러오기 차단"));
	Check(!Widget->GetWidgetFromName(TEXT("LoadSelectedPreset"))->GetIsEnabled(), TEXT("전투 중 편집 UI 비활성"));
	Finish();
}

void ALSAutoBattleProbe::TickExpandedBattlefield(float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 30 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("확장 맵 초기화"));
		Finish();
		return;
	}
	if (Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
	{
		return;
	}
	if (Stage == 0)
	{
		Check(Mode->GetScenario()->BoundsMin.Equals(FVector2D(-4800, -3600)) &&
		          Mode->GetScenario()->BoundsMax.Equals(FVector2D(4800, 3600)),
		      TEXT("확장 전장 경계 저장"));
		Check(Mode->GetScenario()->DeploymentMin.Equals(FVector2D(-4400, -3200)) &&
		          Mode->GetScenario()->DeploymentMax.Equals(FVector2D(-300, 3200)),
		      TEXT("확장 아군 배치 경계 저장"));
		int32 Id = 0;
		Check(Mode->PlaceUnit(ELSTroopType::Spearman, FVector(-4000, -2800, 0), Id) == ELSAutoBattleRequest::Success,
		      TEXT("확장 남서 구역 병력 배치"));
		FirstUnit = Mode->GetPlacementActor(Id);
		Check(Mode->PlaceUnit(ELSTroopType::Spearman, FVector(-4000, 2800, 0), Id) == ELSAutoBattleRequest::Success,
		      TEXT("확장 북서 구역 병력 배치"));
		Check(Mode->PlaceUnit(ELSTroopType::Cavalry, FVector(4000, 2800, 0), Id, ELSFaction::Enemy) ==
		          ELSAutoBattleRequest::Success,
		      TEXT("확장 북동 구역 적 배치"));
		for (int32 Sign : {-1, 1})
		{
			auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			    GetWorld(), FVector(-4000, Sign * 2800, 90), FVector(4000, -Sign * 2800, 90));
			Check(Path && Path->IsValid() && !Path->IsPartial(), TEXT("확장 구역의 반대 모서리까지 완전한 경로"));
		}
		if (!FirstUnit.IsValid())
		{
			Finish();
			return;
		}
		auto* AI = Cast<AAIController>(FirstUnit->GetController());
		FirstUnit->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Check(AI && AI->MoveToLocation(FVector(-3000, -2800, 0), 5, false, true, false, false, nullptr, false) !=
		                EPathFollowingRequestResult::Failed,
		      TEXT("확장 구역 실제 이동 요청"));
		Stage = 1;
		StageTime = 0;
	}
	else if (StageTime > 2)
	{
		Check(FirstUnit.IsValid() && FirstUnit->GetActorLocation().X > -3850, TEXT("확장 지면 위에서 실제 병력 이동"));
		Finish();
	}
}

void ALSAutoBattleProbe::TickPlacementBrush(float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 30 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("브러시 초기화"));
		Finish();
		return;
	}
	if (Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
	{
		return;
	}
	auto* Controller = Cast<ALSAutoBattleController>(GetWorld()->GetFirstPlayerController());
	auto* Widget = Controller ? Controller->GetBattleWidget() : nullptr;
	if (!Widget)
	{
		return;
	}
	Mode->ResetFormation(ELSFaction::Enemy);
	Mode->ResetFormation(ELSFaction::Ally);
	const int32 OriginalBudget = Mode->GetBudget();
	int32 AllyId = 0;
	Check(Mode->PlaceUnit(ELSTroopType::Spearman, FVector(-1200, -2200, 0), AllyId) == ELSAutoBattleRequest::Success,
	      TEXT("다른 진영 보존용 병력"));
	Controller->SetEditingFaction(ELSFaction::Enemy);
	Controller->ChooseTroop(ELSTroopType::Spearman);
	const FVector Start(700, -2200, 0), End(1600, -2200, 0);
	Controller->BeginBrushStroke(Start);
	Controller->ContinueBrushStroke(End);
	Controller->EndBrushStroke();
	Check(Mode->GetScenario()->EnemyFormation.Num() == 7, TEXT("빠른 한 프레임 드래그도 간격에 맞춰 연속 생성"));
	Controller->ContinueBrushStroke(End + FVector(300, 0, 0));
	Check(Mode->GetScenario()->EnemyFormation.Num() == 7, TEXT("버튼 해제 뒤 추가 생성 없음"));
	Controller->BeginBrushStroke(Start);
	Controller->ContinueBrushStroke(End);
	Controller->EndBrushStroke();
	Check(Mode->GetScenario()->EnemyFormation.Num() == 7, TEXT("다시 칠해도 겹쳐 생성하지 않음"));
	int32 OutsideId = 0;
	Check(Mode->PlaceUnit(ELSTroopType::Archer, FVector(700, -1800, 0), OutsideId, ELSFaction::Enemy) ==
	          ELSAutoBattleRequest::Success,
	      TEXT("브러시 밖 보존용 병력"));
	if (auto* Button = Cast<UButton>(Widget->GetWidgetFromName(TEXT("Remove"))))
	{
		Button->OnClicked.Broadcast();
	}
	Check(Controller->IsErasing(), TEXT("UI 삭제 버튼으로 삭제 모드 활성"));
	Controller->BeginBrushStroke(FVector(-1500, -2200, 0));
	Controller->ContinueBrushStroke(FVector(2000, -2200, 0));
	Controller->EndBrushStroke();
	Check(Mode->GetScenario()->EnemyFormation.Num() == 1 && Mode->GetPlacementActor(OutsideId),
	      TEXT("빠른 드래그 경로의 병력만 삭제"));
	Check(Mode->GetPlacementActor(AllyId) != nullptr, TEXT("선택하지 않은 진영은 삭제하지 않음"));
	Check(IsValid(Mode->GetAllyCastle()) && IsValid(Mode->GetEnemyCastle()), TEXT("브러시는 성을 삭제하지 않음"));
	Controller->ChooseTroop(ELSTroopType::Cavalry);
	Check(!Controller->IsErasing(), TEXT("병종 선택은 생성 모드로 복귀"));
	const FVector SecondRow(700, -1400, 0);
	Controller->BeginBrushStroke(SecondRow);
	for (int32 Index = 1; Index <= 90; ++Index)
	{
		Controller->ContinueBrushStroke(SecondRow + FVector(Index * 10, 0, 0));
	}
	Controller->ContinueBrushStroke(SecondRow + FVector(900, 0, 0));
	Controller->EndBrushStroke();
	Check(Mode->GetScenario()->EnemyFormation.Num() == 8, TEXT("작은 프레임 이동도 같은 생성 밀도·기마병 간격 유지"));
	Controller->BeginBrushStroke(FVector(700, -1000, 0));
	const int32 BeforeToolChange = Mode->GetScenario()->EnemyFormation.Num();
	Controller->ChooseTroop(ELSTroopType::Archer);
	Controller->ContinueBrushStroke(FVector(1600, -1000, 0));
	Check(Mode->GetScenario()->EnemyFormation.Num() == BeforeToolChange, TEXT("병종 변경으로 이전 스트로크 종료"));
	Controller->ToggleEraseBrush();
	Controller->BeginBrushStroke(FVector(700, -1800, 0));
	Controller->EndBrushStroke();
	Check(!Mode->GetPlacementActor(OutsideId), TEXT("드래그 없는 단일 클릭 삭제"));
	Controller->SetEditingFaction(ELSFaction::Ally);
	Controller->ChooseTroop(ELSTroopType::Spearman);
	Controller->BeginBrushStroke(FVector(-2000, -2600, 0));
	Controller->ContinueBrushStroke(FVector(-1550, -2600, 0));
	Controller->EndBrushStroke();
	Check(Mode->GetPopulation() == 5 &&
	          Mode->GetBudget() ==
	              OriginalBudget - 5 * Mode->GetScenario()->UnitCosts.FindChecked(ELSTroopType::Spearman),
	      TEXT("생성은 공통 예산·인구 계산 사용"));
	Controller->ToggleEraseBrush();
	Controller->BeginBrushStroke(FVector(-2200, -2600, 0));
	Controller->ContinueBrushStroke(FVector(-1300, -2600, 0));
	Controller->EndBrushStroke();
	Check(Mode->GetPopulation() == 1 &&
	          Mode->GetBudget() == OriginalBudget - Mode->GetScenario()->UnitCosts.FindChecked(ELSTroopType::Spearman),
	      TEXT("삭제는 제거한 병력의 비용만 환급"));
	Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("브러시 편성으로 전투 시작"));
	const int32 BeforeBattleEdit = Mode->GetScenario()->EnemyFormation.Num();
	Controller->BeginBrushStroke(Start);
	Controller->ContinueBrushStroke(End);
	Check(!Controller->bBrushStroke && Mode->GetScenario()->EnemyFormation.Num() == BeforeBattleEdit,
	      TEXT("전투 중 브러시 차단"));
	Finish();
}

void ALSAutoBattleProbe::TickDenseRetry(float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (!Mode.IsValid())
	{
		Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	}
	if (!Mode.IsValid() || Elapsed > 100 || Mode->GetPhase() == ELSAutoBattlePhase::Error)
	{
		Check(false, TEXT("밀집 편성 복원 초기화"));
		Finish();
		return;
	}
	if (Stage == 0 && Mode->GetPhase() == ELSAutoBattlePhase::Preparation)
	{
		Mode->ResetFormation(ELSFaction::Ally);
		Mode->ResetFormation(ELSFaction::Enemy);
		Mode->SetPreparationLimits(10000, 100);
		Check(Mode->MoveCastle(FVector(-3200, -1400, 0), ELSFaction::Ally) == ELSAutoBattleRequest::Success &&
		          Mode->MoveCastle(FVector(2400, -1400, 0), ELSFaction::Enemy) == ELSAutoBattleRequest::Success,
		      TEXT("양 진영 성 시작 위치 변경"));

		FRandomStream Random(76431);
		for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
		{
			int32 Count = 0;
			for (int32 Attempt = 0; Attempt < 3000 && Count < 70; ++Attempt)
			{
				int32 Id;
				const float X =
				    Faction == ELSFaction::Ally ? Random.FRandRange(-2400, -700) : Random.FRandRange(100, 1800);
				const FVector Ground(X, Random.FRandRange(-2200, -600), 0);
				if (Mode->PlaceUnit(static_cast<ELSTroopType>(Attempt % 3), Ground, Id, Faction) ==
				    ELSAutoBattleRequest::Success)
				{
					++Count;
				}
			}
			Check(Count == 70, TEXT("혼합 병종 밀집 편성 배치"));
		}
		BudgetAfterPlacement = Mode->GetBudget();
		Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("밀집 편성 전투 시작"));
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 20)
	{
		Mode->GetEnemyCastle()->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		    ULSHealthComponent::MakeDamageRequest(100000, nullptr, nullptr));
		for (const auto& Placement : Mode->GetScenario()->EnemyFormation)
		{
			if (ALSUnit* Unit = Mode->GetPlacementActor(Placement.Id))
			{
				Unit->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				    ULSHealthComponent::MakeDamageRequest(100000, nullptr, nullptr));
			}
		}
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && Mode->GetPhase() == ELSAutoBattlePhase::Victory)
	{
		Check(Mode->RetryBattle() == ELSAutoBattleRequest::Success, TEXT("교전 후 밀집 편성 복원 요청"));
		Stage = 3;
		StageTime = 0;
	}
	else if (Stage == 3 && Mode->GetPhase() == ELSAutoBattlePhase::Preparation)
	{
		Check(Mode->GetPopulation() == 70 && Mode->GetScenario()->EnemyFormation.Num() == 70 &&
		          Mode->GetBudget() == BudgetAfterPlacement,
		      TEXT("양 진영 편성과 예산 복원"));
		bool bExact = true;
		const TArray<FLSAutoBattlePlacement>* Formations[] = {&Mode->GetFormation(),
		                                                      &Mode->GetScenario()->EnemyFormation};
		for (const auto* Formation : Formations)
		{
			for (const auto& Placement : *Formation)
			{
				ALSUnit* Unit = Mode->GetPlacementActor(Placement.Id);
				bExact &=
				    Unit && HasFullHealth(Unit) && FVector::Dist2D(Unit->GetActorLocation(), Placement.Position) < 0.1;
			}
		}
		bExact &= FVector::Dist2D(Mode->GetAllyCastle()->GetActorLocation(), FVector(-3200, -1400, 0)) < 0.1 &&
		          FVector::Dist2D(Mode->GetEnemyCastle()->GetActorLocation(), FVector(2400, -1400, 0)) < 0.1 &&
		          HasFullHealth(Mode->GetAllyCastle()) && HasFullHealth(Mode->GetEnemyCastle());

		Check(bExact, TEXT("모든 병력의 저장 XY 좌표와 체력 복원"));

		Check(Mode->StartBattle() == ELSAutoBattleRequest::Success, TEXT("복원된 편성으로 재전투 시작"));
		if (++MatchupRound >= 2)
		{
			Finish();
			return;
		}
		Stage = 1;
		StageTime = 0;
	}
}
