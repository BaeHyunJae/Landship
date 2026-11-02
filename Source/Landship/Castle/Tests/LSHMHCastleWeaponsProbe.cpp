#include "Castle/Tests/LSHMHCastleWeaponsProbe.h"

#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSHUD.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSCommandCardComponent.h"
#include "HUD/LSBattleHUDWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Match/LSGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Troops/LSTroopUnit.h"
#include "Vision/LSVisionSubsystem.h"

namespace
{
float CurrentHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	return Actor->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}
} // namespace

ALSHMHCastleWeaponsProbe::ALSHMHCastleWeaponsProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSHMHCastleWeaponsProbe::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHCastleWeaponsTestExit")))
	{
		auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
		bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
		bOriginalSelfDamage = Settings->bAllowSelfDamage;
		bOverridesDamagePolicy = true;
		Settings->bAllowFriendlyFire = false;
		Settings->bAllowSelfDamage = true;
		return;
	}
#endif
	SetActorTickEnabled(false);
	Destroy();
}

void ALSHMHCastleWeaponsProbe::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bOverridesDamagePolicy)
	{
		auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
		bOverridesDamagePolicy = false;
	}
	Super::EndPlay(EndPlayReason);
}

void ALSHMHCastleWeaponsProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("HMH_WEAPONS %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Message);
}

void ALSHMHCastleWeaponsProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_WEAPONS_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSHMHCastleWeaponsProbe::Capture()
{
	// UMG는 고해상도 장면 캡처에서 제외되므로 뷰포트 UI를 포함하는 캡처를 사용한다.
	GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot ShowUI"));
}

void ALSHMHCastleWeaponsProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 22)
	{
		Check(false, TEXT("무장 실행 검사 시간 제한"));
		Finish();
		return;
	}
	ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Stage == 0 && StageTime > 1.5f)
	{
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			It->Orders->SetComponentTickEnabled(false);
			It->BasicAttack->StopAttack();
			if (ALSMobileCastle* Found = Cast<ALSMobileCastle>(*It))
			{
				if (Found->GetFaction() == ELSFaction::Ally)
					Castle = Found;
				else
					EnemyCastle = Found;
			}
			else if (ALSTroopUnit* Unit = Cast<ALSTroopUnit>(*It))
			{
				if (Unit->GetFaction() == ELSFaction::Enemy)
					Enemies.Add(Unit);
				else
				{
					Ally = Unit;
					Unit->SetActorLocation(FVector(-1600, -1200 + Enemies.Num() * 150, 96));
				}
			}
		}
		Check(Controller && Controller->GetHUD() && Castle && EnemyCastle && Enemies.Num() == 3 && Ally,
		      TEXT("실제 맵의 성·병력·HUD 연결"));
		if (!Controller || !Controller->GetHUD() || !Castle || !EnemyCastle || Enemies.Num() != 3 || !Ally)
		{
			Finish();
			return;
		}

		Check(GetWorld()->GetSubsystem<ULSVisionSubsystem>()->IsVisionEnabled(), TEXT("HMH 시야 설정 활성"));
		Controller->ApplySelection({Ally, Castle});
		Controller->HandleMinimapInput(FVector(-600, -800, 0), true, false);
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::Move &&
		          Castle->Orders->GetOrderKind() == ELSOrderKind::Move,
		      TEXT("미니맵 우클릭으로 병력·성 함께 이동"));
		Controller->HandleMinimapInput(FVector(-400, -800, 0), true, true);
		Check(Ally->Orders->GetQueuedCount() == 1 && Castle->Orders->GetQueuedCount() == 1,
		      TEXT("미니맵 Shift 우클릭 예약"));
		Controller->SetPendingInput(ELSCommandInput::Attack);
		Controller->HandleMinimapInput(FVector(-600, -800, 0), false, false);
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::AttackMove &&
		          Castle->Orders->GetOrderKind() == ELSOrderKind::Move,
		      TEXT("미니맵 어택땅은 공격 가능한 병력에만 적용"));
		Check(Controller->GetPendingInput() == ELSCommandInput::None, TEXT("미니맵 명령 후 대기 종료"));
		Controller->IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false);
		Castle->SetActorLocation(FVector(-800, 100, 144));
		EnemyCastle->SetActorLocation(FVector(1600, -1200, 144));
		Enemies[0]->SetActorLocation(FVector(0, 100, 96));
		Enemies[1]->SetActorLocation(FVector(0, 280, 96));
		Enemies[2]->SetActorLocation(FVector(0, 700, 96));
		// 배치 변경 직후의 지정 공격도 실제 시야 스냅샷으로 판정한다.
		ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
		Vision->RequestRefresh();
		Vision->Tick(0.11f);
		Controller->ApplySelection({Ally});
		Check(Controller->IssueSelectionOrder(ELSOrderKind::AttackTarget, FVector::ZeroVector, Enemies[0], false),
		      TEXT("적 지정 공격 피드백 요청 접수"));
		Check(Controller->GetAttackCommandMarker() == Enemies[0] && Controller->GetSelectedActors().Contains(Ally),
		      TEXT("아군 선택을 유지하며 적 원형 표시"));
		Controller->ApplySelection({Castle});
		Check(!Castle->Orders->CanAttack() && !Castle->BasicAttack->IsConfigured(), TEXT("성의 기본 공격 제거"));
		Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon);
		Controller->CancelInput();
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) == 0, TEXT("조준 취소는 쿨타임 미소모"));
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		ALSRTSHUD* HUD = Cast<ALSRTSHUD>(Controller->GetHUD());
		// 선택 직후 같은 틱에서 클릭하므로, 사용자가 보게 될 카드를 먼저 갱신한다.
		if (HUD && HUD->GetBattleWidget())
			HUD->GetBattleWidget()->RefreshState();
		const int32 Population = GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation();
		Check(HUD && HUD->HandleClick(HUD->GetCommandCellRect(5).GetCenter()), TEXT("Q 무장 버튼 클릭"));
		Check(Controller->GetPendingInput() == ELSCommandInput::CastleWeapon, TEXT("버튼 클릭은 발사 대신 조준 대기"));
		Check(GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation() == Population,
		      TEXT("Q는 더 이상 병력을 생산하지 않음"));
		Check(Controller->ConfirmCastleWeapon(FVector(0, 100, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("원형 대포 확인 시전"));
		Check(CurrentHealth(Enemies[0]) == 120 && CurrentHealth(Enemies[1]) == 120, TEXT("포탄 발사 직후 피해 없음"));
		Capture();
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 0.5f)
	{
		Check(CurrentHealth(Enemies[0]) == 60 && CurrentHealth(Enemies[1]) == 60 && CurrentHealth(Enemies[2]) == 120,
		      TEXT("착탄 후 원형 구역 적들만 피해"));
		Enemies[0]->SetActorLocation(FVector(-400, 100, 96));
		Enemies[1]->SetActorLocation(FVector(-400, 500, 96));
		EnemyCastle->SetActorLocation(FVector(-350, 300, 144));
		Ally->SetActorLocation(FVector(-400, -100, 96));
		PushStartX = Enemies[0]->GetActorLocation().X;
		// 기본 근접 사거리로 넉백 후 재추적과 예약 보존을 확인한다.
		Enemies[0]->Orders->SetComponentTickEnabled(true);
		Enemies[0]->Selection->SetPlayerControlled(true);
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Castle;
		Check(Enemies[0]->Orders->SubmitOrder(Attack), TEXT("넉백 전 지정 공격 준비"));
		FLSOrder Queued;
		Queued.Kind = ELSOrderKind::Move;
		Queued.Destination = FVector(-300, 900, 0);
		Check(Enemies[0]->Orders->SubmitOrder(Queued, true), TEXT("넉백 전 예약 명령 준비"));
		Enemies[0]->Selection->SetPlayerControlled(false);
		Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun);
		Check(Controller->ConfirmCastleWeapon(FVector(200, 100, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("부채꼴 산탄 시전"));
		Check(CurrentHealth(Enemies[0]) == 60, TEXT("산탄 발사 직후 피해 없음"));
		Stage = 6;
		StageTime = 0;
	}
	else if (Stage == 6 && StageTime > 0.25f)
	{
		Check(GetWorld()->GetSubsystem<ULSVisionSubsystem>()->HasDiscoveredEnemy() &&
		          !GetWorld()->GetSubsystem<ULSVisionSubsystem>()->HasPendingDiscoveryCaption(),
		      TEXT("최초 적 발견 알림을 실제 HUD에서 표시"));
		Check(Enemies[0]->GetCharacterMovement()->Velocity.X > 100, TEXT("실제 바닥에서 적 병력이 밀려남"));
		Check(Enemies[0]->Orders->IsKnockedBack() && Enemies[0]->Orders->GetQueuedCount() == 1,
		      TEXT("넉백 중 기존 명령·예약 보존"));
		Check(CurrentHealth(Enemies[0]) < 60 && CurrentHealth(Enemies[0]) >= 25 && CurrentHealth(Enemies[1]) == 60,
		      TEXT("부채꼴 안·밖 판정"));
		Check(CurrentHealth(EnemyCastle) < 1000 && CurrentHealth(EnemyCastle) >= 965 && CurrentHealth(Ally) == 120,
		      TEXT("오폭 금지 시 적 성은 피해 적용·아군 제외"));
		Capture();
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 0.35f && !Enemies[0]->Orders->IsKnockedBack())
	{
		Check(!Enemies[0]->Orders->IsKnockedBack() &&
		          Enemies[0]->Orders->GetOrderKind() == ELSOrderKind::AttackTarget &&
		          Enemies[0]->Orders->GetQueuedCount() == 1,
		      TEXT("착지 후 지정 공격 재개·예약 유지"));
		UE_LOG(LogTemp, Display, TEXT("HMH_PROJECTILE_ORDER kind=%s queue=%d location=%s"),
		       *UEnum::GetValueAsString(Enemies[0]->Orders->GetOrderKind()), Enemies[0]->Orders->GetQueuedCount(),
		       *Enemies[0]->GetActorLocation().ToString());
		Enemies[0]->Orders->SetComponentTickEnabled(false);
		Enemies[0]->BasicAttack->StopAttack();
		Check(FVector::Dist2D(EnemyCastle->GetActorLocation(), FVector(-350, 300, 144)) < 5,
		      TEXT("성은 산탄 넉백 제외"));
		EnemyCastle->SetActorLocation(FVector(1600, -1200, 144));
		Enemies[0]->GetCharacterMovement()->StopMovementImmediately();
		Enemies[0]->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Enemies[0]->SetActorLocation(FVector(-200, 100, 96));
		Enemies[1]->SetActorLocation(FVector(200, 100, 96));
		Ally->SetActorLocation(FVector(-550, 100, 96));
		Controller->ArmCastleWeapon(ELSCastleWeapon::Ballista);
		Check(Controller->ConfirmCastleWeapon(FVector(1000, 100, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("직선 발리스타 시전"));
		Capture();
		Stage = 3;
		StageTime = 0;
	}
	else if (Stage == 3 && StageTime > 0.7f)
	{
		Check(CurrentHealth(Enemies[0]) == 0 && CurrentHealth(Enemies[1]) == 60, TEXT("발리스타는 첫 적만 명중"));
		Check(CurrentHealth(Ally) == 120, TEXT("오폭 금지 시 발리스타는 앞의 아군을 통과"));
		UE_LOG(LogTemp, Display, TEXT("HMH_PROJECTILE_DIAG ally_hp=%.2f castle_hp=%.2f selected=%d"),
		       CurrentHealth(Ally), CurrentHealth(Castle), Controller->GetSelectedActors().Num());
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) > 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun) > 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista) > 0,
		      TEXT("세 무장 독립 쿨타임 진행"));
		Stage = 4;
		Castle->SetActorLocation(FVector(-800, -1100, 144));
		Castle->Orders->SetComponentTickEnabled(true);
		Check(Controller->GetPendingInput() == ELSCommandInput::None, TEXT("산탄 이후 조준 입력 해제"));
		Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(-400, -800, 0), nullptr, false),
		      TEXT("재현 순서의 첫 성 이동 명령 접수"));
	}
	else if (Stage == 4 && Elapsed > 10)
	{
		Check(Castle->GetActorLocation().X > -700, TEXT("재현 순서의 첫 성 실제 이동"));
		PostShotMoveStart = Castle->GetActorLocation();
		Check(Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun) &&
		          Controller->ConfirmCastleWeapon(FVector(1000, -800, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("이동한 위치에서 산탄 발사"));
		Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(-1000, -1200, 0), nullptr, false),
		      TEXT("이동·산탄 이후 새 경로 접수"));
		Stage = 7;
		StageTime = 0;
	}
	else if (Stage == 7 && StageTime > 1)
	{
		Check(FVector::Dist2D(PostShotMoveStart, Castle->GetActorLocation()) > 50, TEXT("이동·산탄 이후 성 재이동"));
		// 생산 검사는 원래 지도에서 이동 가능하게 배치한 성 위치에서 별도로 수행한다.
		Castle->SetActorLocation(FVector(-800, -1100, 144));
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) == 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun) > 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista) > 0,
		      TEXT("재발사한 산탄과 다른 무장의 독립 쿨타임"));
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		const int32 Before = GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation();
		Cast<ALSRTSHUD>(Controller->GetHUD())
		    ->HandleClick(Cast<ALSRTSHUD>(Controller->GetHUD())->GetCommandCellRect(10).GetCenter());
		Check(GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation() == Before + 1,
		      TEXT("F1 생산 버튼은 기존 생산 기능 유지"));
		UE_LOG(LogTemp, Display, TEXT("HMH_PROJECTILE_PRODUCTION result=%s before=%d after=%d"),
		       *UEnum::GetValueAsString(Controller->GetLastProductionResult()), Before,
		       GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation());
		Check(Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon) == ELSWeaponChangeResult::Success,
		      TEXT("실제 성의 무장 2레벨 강화"));
		Check(Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Shotgun, false) == ELSWeaponChangeResult::Success &&
		          Castle->Weapons->GetStatus(ELSCastleWeapon::Shotgun) == ELSWeaponCastResult::Disabled,
		      TEXT("실제 성의 무장 비활성화"));
		Check(Castle->Weapons->SetWeaponUnlocked(ELSCastleWeapon::Ballista, false) == ELSWeaponChangeResult::Success &&
		          Castle->Weapons->GetStatus(ELSCastleWeapon::Ballista) == ELSWeaponCastResult::Locked,
		      TEXT("실제 성의 무장 잠금"));
		Capture();
		Stage = 5;
		StageTime = 0;
	}
	else if (Stage == 5 && StageTime > 0.2f)
	{
		Controller->ApplySelection({Ally});
		const auto Cells = Controller->CommandCard->BuildCard();
		Check(!Cells[0].Entry.Id.IsNone() && Cells[5].Entry.Id.IsNone(), TEXT("병력 선택 시 공통 명령만 표시"));
		Stage = 11;
		StageTime = 0;
	}
	else if (Stage == 11 && StageTime > 0.2f)
	{
		Capture();
		Stage = 12;
		StageTime = 0;
	}
	else if (Stage == 12 && StageTime > 0.2f)
	{
		Controller->ApplySelection({Castle, Ally});
		Controller->CommandCard->CycleGroup();
		Check(!Controller->CommandCard->BuildCard()[5].Entry.Id.IsNone(),
		      TEXT("혼합 선택의 Tab 전환으로 성 무장 표시"));
		Stage = 13;
		StageTime = 0;
	}
	else if (Stage == 13 && StageTime > 0.2f)
	{
		Capture();
		Stage = 14;
		StageTime = 0;
	}
	else if (Stage == 14 && StageTime > 0.2f)
	{
		Finish();
	}
}
