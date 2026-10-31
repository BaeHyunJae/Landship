#include "Castle/Tests/LSHMHCastleWeaponsProbe.h"

#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSHUD.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Match/LSGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Troops/LSTroopUnit.h"

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
		return;
#endif
	SetActorTickEnabled(false);
	Destroy();
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
	GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("HighResShot 1600x900"));
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
		Castle->SetActorLocation(FVector(-800, 100, 144));
		EnemyCastle->SetActorLocation(FVector(1600, -1200, 144));
		Enemies[0]->SetActorLocation(FVector(0, 100, 96));
		Enemies[1]->SetActorLocation(FVector(0, 280, 96));
		Enemies[2]->SetActorLocation(FVector(0, 700, 96));
		Controller->ApplySelection({Castle});
		Check(!Castle->Orders->CanAttack() && !Castle->BasicAttack->IsConfigured(), TEXT("성의 기본 공격 제거"));
		Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon);
		Controller->CancelInput();
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) == 0, TEXT("조준 취소는 쿨타임 미소모"));
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		ALSRTSHUD* HUD = Cast<ALSRTSHUD>(Controller->GetHUD());
		const int32 Population = GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation();
		Check(HUD && HUD->HandleClick(FVector2D(Width - 360 + 64, Height - 336 + 80)), TEXT("Q 무장 버튼 클릭"));
		Check(Controller->GetPendingInput() == ELSCommandInput::CastleWeapon, TEXT("버튼 클릭은 발사 대신 조준 대기"));
		Check(GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation() == Population,
		      TEXT("Q는 더 이상 병력을 생산하지 않음"));
		Check(Controller->ConfirmCastleWeapon(FVector(0, 100, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("원형 대포 확인 시전"));
		Check(CurrentHealth(Enemies[0]) == 60 && CurrentHealth(Enemies[1]) == 60 && CurrentHealth(Enemies[2]) == 120,
		      TEXT("원형 구역의 적들만 피해"));
		Capture();
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 0.5f)
	{
		Enemies[0]->SetActorLocation(FVector(-400, 100, 96));
		Enemies[1]->SetActorLocation(FVector(-400, 500, 96));
		EnemyCastle->SetActorLocation(FVector(-350, 300, 144));
		Ally->SetActorLocation(FVector(-400, -100, 96));
		PushStartX = Enemies[0]->GetActorLocation().X;
		// 실제 추적 AI가 켜져 있어도 넉백이 취소되지 않는지 확인한다.
		Enemies[0]->Orders->SetComponentTickEnabled(true);
		Enemies[0]->Selection->SetPlayerControlled(true);
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Castle;
		Enemies[0]->Orders->SubmitOrder(Attack);
		FLSOrder Queued;
		Queued.Kind = ELSOrderKind::Move;
		Queued.Destination = FVector(-300, 900, 0);
		Check(Enemies[0]->Orders->SubmitOrder(Queued, true), TEXT("넉백 전 예약 명령 준비"));
		Enemies[0]->Selection->SetPlayerControlled(false);
		Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun);
		Check(Controller->ConfirmCastleWeapon(FVector(200, 100, 0)) == ELSWeaponCastResult::Ready,
		      TEXT("부채꼴 산탄 시전"));
		Check(Enemies[0]->Orders->IsKnockedBack() && Enemies[0]->Orders->GetQueuedCount() == 1,
		      TEXT("넉백 중 기존 명령·예약 보존"));
		Check(CurrentHealth(Enemies[0]) == 25 && CurrentHealth(Enemies[1]) == 60, TEXT("부채꼴 안·밖 판정"));
		Check(CurrentHealth(EnemyCastle) == 965 && CurrentHealth(Ally) == 120, TEXT("적 성은 피해 적용·아군 제외"));
		Capture();
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 0.55f)
	{
		Check(Enemies[0]->GetActorLocation().X > PushStartX + 60, TEXT("실제 바닥에서 적 병력이 밀려남"));
		Check(!Enemies[0]->Orders->IsKnockedBack() &&
		          Enemies[0]->Orders->GetOrderKind() == ELSOrderKind::AttackTarget &&
		          Enemies[0]->Orders->GetQueuedCount() == 1,
		      TEXT("착지 후 추적 재개·예약 유지"));
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
		Check(CurrentHealth(Ally) == 120, TEXT("발리스타는 앞의 아군을 통과"));
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) > 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun) > 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista) > 0,
		      TEXT("세 무장 독립 쿨타임 진행"));
		Stage = 4;
	}
	else if (Stage == 4 && Elapsed > 10)
	{
		Check(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon) == 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun) == 0 &&
		          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista) > 0,
		      TEXT("8·6·10초의 독립 쿨타임 해제"));
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		const int32 Before = GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation();
		Cast<ALSRTSHUD>(Controller->GetHUD())->HandleClick(FVector2D(Width - 360 + 64, Height - 180 + 95));
		Check(GetWorld()->GetGameState<ALSGameState>()->GetCurrentPopulation() == Before + 1,
		      TEXT("F1 생산 버튼은 기존 생산 기능 유지"));
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
		Finish();
	}
}
