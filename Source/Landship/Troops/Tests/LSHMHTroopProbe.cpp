#include "Troops/Tests/LSHMHTroopProbe.h"

#include "Troops/LSTroopUnit.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

ALSHMHTroopProbe::ALSHMHTroopProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSHMHTroopProbe::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHTroopTypesTestExit")))
	{
		return;
	}
#endif
	// 일반 플레이에서는 검증 액터를 남기지 않는다.
	SetActorTickEnabled(false);
	Destroy();
}

void ALSHMHTroopProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("HMH_TROOPS %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Message);
}

void ALSHMHTroopProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_TROOPS_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSHMHTroopProbe::Hold(ALSTroopUnit* Unit)
{
	const bool bControlled = Unit->Selection->CanPlayerControl();
	Unit->Selection->SetPlayerControlled(true);
	FLSOrder Order;
	Order.Kind = ELSOrderKind::Hold;
	Check(Unit->Orders->SubmitOrder(Order), TEXT("검사용 위치 사수"));
	Unit->Selection->SetPlayerControlled(bControlled);
}

float ALSHMHTroopProbe::GetHealth(ALSTroopUnit* Unit) const
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Health = IsValid(Unit) ? Unit->FindComponentByClass<ULSHealthComponent>() : nullptr;
	return Health && Health->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

void ALSHMHTroopProbe::OnSpearHit(AActor* Target, float Damage)
{
	++SpearHits;
}
void ALSHMHTroopProbe::OnArcherHit(AActor* Target, float Damage)
{
	++ArcherHits;
}
void ALSHMHTroopProbe::OnCavalryHit(AActor* Target, float Damage)
{
	++CavalryHits;
}

void ALSHMHTroopProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 45)
	{
		Check(false, TEXT("병종 검사 시간 제한"));
		Finish();
		return;
	}
	if (Stage == 0 && StageTime > 1.5f)
	{
		for (TActorIterator<ALSMobileCastle> It(GetWorld()); It; ++It)
		{
			(It->GetFaction() == ELSFaction::Ally ? Castle : EnemyCastle) = *It;
			It->Selection->SetPlayerControlled(true);
			FLSOrder HoldOrder;
			HoldOrder.Kind = ELSOrderKind::Hold;
			It->Orders->SubmitOrder(HoldOrder);
			It->SetActorLocation(FVector(It->GetFaction() == ELSFaction::Ally ? -1600 : 1700, -1500, 144));
		}
		Check(Castle && EnemyCastle, TEXT("양 진영 대형 성 배치"));
		if (!Castle || !EnemyCastle)
		{
			Finish();
			return;
		}
		Allies.SetNum(3);
		Enemies.SetNum(3);
		for (TActorIterator<ALSTroopUnit> It(GetWorld()); It; ++It)
		{
			const int32 Index = static_cast<int32>(It->GetTroopType());
			if (Index >= 0 && Index < 3)
			{
				(It->GetFaction() == ELSFaction::Ally ? Allies : Enemies)[Index] = *It;
			}
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Check(IsValid(Allies[Index]) && IsValid(Enemies[Index]), TEXT("양 진영 세 병종 배치"));
			if (!Allies[Index] || !Enemies[Index])
			{
				Finish();
				return;
			}
			Enemies[Index]->SetActorLocation(FVector(1700, -1100 + Index * 300, 96));
			Hold(Enemies[Index]);
			const FVector Start(-1300, 700 + Index * 300, 96);
			Allies[Index]->SetActorLocation(Start);
			Starts.Add(Start);
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = FVector(1200, Start.Y, 0);
			Check(Allies[Index]->Orders->SubmitOrder(Move), TEXT("병종별 동일 거리 이동 요청"));
			// 기본 속도 비교에서는 서로의 회피로 발생하는 감속을 분리한다.
			Allies[Index]->GetCharacterMovement()->SetAvoidanceEnabled(false);
		}
		Allies[0]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHTroopProbe::OnSpearHit);
		Allies[1]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHTroopProbe::OnArcherHit);
		Allies[2]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHTroopProbe::OnCavalryHit);
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime <= 1.5f && StageTime > 0.4f)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector Velocity = Allies[Index]->GetVelocity();
			Check(Velocity.Size2D() > 100, TEXT("반복 명령 사이 실제 이동 속도 유지"));
			FLSOrder Move;
			Move.Kind = (RepeatMoves % 2 == 0) ? ELSOrderKind::Move : ELSOrderKind::AttackMove;
			Move.Destination = FVector(1200, Starts[Index].Y + ((RepeatMoves % 2 == 0) ? 40 : -40), 0);
			Check(Allies[Index]->Orders->SubmitOrder(Move), TEXT("이동 중 목적지 반복 교체"));
			Allies[Index]->GetCharacterMovement()->SetAvoidanceEnabled(false);
			Check(Allies[Index]->GetVelocity().Equals(Velocity, 0.01), TEXT("명령 교체 순간 속도 초기화 없음"));
			Check(Allies[Index]->Orders->GetOrderKind() == Move.Kind, TEXT("이전 요청 중단 알림이 새 명령을 종료하지 않음"));
		}
		++RepeatMoves;
	}
	else if (Stage == 1 && StageTime > 1.5f)
	{
		const double SpearDistance = FVector::Dist2D(Starts[0], Allies[0]->GetActorLocation());
		const double ArcherDistance = FVector::Dist2D(Starts[1], Allies[1]->GetActorLocation());
		const double CavalryDistance = FVector::Dist2D(Starts[2], Allies[2]->GetActorLocation());
		UE_LOG(LogTemp, Display, TEXT("HMH_TROOPS distances spear=%f archer=%f cavalry=%f"), SpearDistance,
		       ArcherDistance, CavalryDistance);
		Check(SpearDistance > 200, TEXT("창병 실제 이동"));
		Check(FMath::Abs(SpearDistance - ArcherDistance) < 35, TEXT("창병과 궁병의 실제 이동 거리 동일"));
		Check(CavalryDistance > SpearDistance * 1.7, TEXT("기마병의 실제 이동이 더 빠름"));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector Velocity = Allies[Index]->GetVelocity();
			FLSOrder Invalid;
			Invalid.Kind = ELSOrderKind::Move;
			Invalid.Destination = FVector(1.e8);
			Check(!Allies[Index]->Orders->SubmitOrder(Invalid), TEXT("도달 불가 목적지 거부"));
			Check(Allies[Index]->GetVelocity().Equals(Velocity, 0.01), TEXT("거부된 이동은 현재 속도 보존"));
			Hold(Allies[Index]);
			Check(Allies[Index]->GetVelocity().IsNearlyZero(), TEXT("위치 사수는 즉시 정지"));
			Allies[Index]->SetActorLocation(FVector(-900, 500 + Index * 500, 96));
		}
		Enemies[0]->SetActorLocation(FVector(-300, 1000, 96));
		PreviousTargetHealth = GetHealth(Enemies[0]);
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 0.8f)
	{
		Check(ArcherHits > 0 && GetHealth(Enemies[0]) < PreviousTargetHealth,
		      TEXT("궁병은 600cm 거리에서 실제 피해 적용"));
		Check(SpearHits == 0 && CavalryHits == 0, TEXT("근접 두 병종은 원거리에서 공격하지 않음"));
		Check(FVector::Dist2D(Allies[1]->GetActorLocation(), FVector(-900, 1000, 96)) < 5,
		      TEXT("궁병 원거리 공격에 접근 이동 불필요"));
		Allies[1]->Orders->SetComponentTickEnabled(false);
		Allies[1]->BasicAttack->StopAttack();
		PreviousTargetHealth = GetHealth(Enemies[0]);
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Enemies[0];
		Check(Allies[0]->Orders->SubmitOrder(Attack), TEXT("창병 지정 공격 요청"));
		Stage = 3;
		StageTime = 0;
	}
	else if (Stage == 3 && SpearHits > 0)
	{
		Check(FVector::Dist(Allies[0]->GetActorLocation(), Enemies[0]->GetActorLocation()) <=
		          Allies[0]->BasicAttack->GetAttackSettings().Range + 1,
		      TEXT("창병은 근접 사거리까지 접근한 뒤 공격"));
		Check(GetHealth(Enemies[0]) < PreviousTargetHealth, TEXT("창병 실제 공통 체력 감소"));
		Allies[0]->Orders->SetComponentTickEnabled(false);
		Allies[0]->BasicAttack->StopAttack();
		PreviousTargetHealth = GetHealth(Enemies[0]);
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Enemies[0];
		Check(Allies[2]->Orders->SubmitOrder(Attack), TEXT("기마병 지정 공격 요청"));
		Stage = 4;
	}
	else if (Stage == 4 && CavalryHits > 0)
	{
		Check(FVector::Dist(Allies[2]->GetActorLocation(), Enemies[0]->GetActorLocation()) <=
		          Allies[2]->BasicAttack->GetAttackSettings().Range + 1,
		      TEXT("기마병도 근접 사거리에서만 공격"));
		Check(GetHealth(Enemies[0]) < PreviousTargetHealth, TEXT("기마병 실제 공통 체력 감소"));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Hold(Allies[Index]);
			Allies[Index]->BasicAttack->StopAttack();
			Allies[Index]->SetActorLocation(FVector(-1600, 800 + Index * 300, 96));
		}
		Castle->SetActorLocation(FVector(-900, -400, 144));
		ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
		Check(Controller != nullptr, TEXT("혼합 조작 컨트롤러"));
		if (Controller)
		{
			Controller->ApplySelection({Castle, Allies[0]}, false, true);
			Check(Controller->GetSelectedActors().Num() == 2, TEXT("성과 병력 혼합 드래그 선택"));
			Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(900, -400, 0), nullptr, false),
			      TEXT("혼합 선택 이동 명령 전달"));
			Check(Castle->Orders->GetOrderKind() == ELSOrderKind::Move &&
			          Allies[0]->Orders->GetOrderKind() == ELSOrderKind::Move,
			      TEXT("성과 병력 양쪽에 이동 명령 적용"));
			Check(Castle->Orders->GetOrderDestination().Equals(Allies[0]->Orders->GetOrderDestination(), 0.01),
			      TEXT("성과 병력의 일반 이동은 같은 클릭 지점으로 집결"));
			Hold(Allies[0]);
		}
		FLSOrder Move;
		Move.Kind = ELSOrderKind::Move;
		Move.Destination = FVector(900, -400, 0);
		Check(Castle->Orders->SubmitOrder(Move), TEXT("대형 성 장애물 반대편 이동 요청"));
		Stage = 5;
		StageTime = 0;
	}
	else if (Stage == 5)
	{
		CastleDetour = FMath::Max(CastleDetour, FMath::Abs(static_cast<float>(Castle->GetActorLocation().Y + 400)));
		if (!bCastleRetargeted && StageTime > 0.6f)
		{
			const FVector Velocity = Castle->GetVelocity();
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = FVector(900, -400, 0);
			Check(Castle->Orders->SubmitOrder(Move) && Castle->GetVelocity().Equals(Velocity, 0.01),
			      TEXT("대형 성 반복 이동 시 속도 보존"));
			bCastleRetargeted = true;
		}
		if (StageTime > 1 && Castle->Orders->GetOrderKind() == ELSOrderKind::Idle)
		{
			Check(FVector::Dist2D(Castle->GetActorLocation(), FVector(900, -400, 0)) < 190,
			      TEXT("대형 성 목적지 도착"));
			Check(CastleDetour > 430, TEXT("성의 몸집을 고려해 벽 바깥으로 우회"));
			EnemyCastle->SetActorLocation(Castle->GetActorLocation() + FVector(600, 0, 0));
			EnemyCastle->BasicAttack->Deactivate();
			EnemyCastle->Orders->SetComponentTickEnabled(false);
			FLSOrder Attack;
			Attack.Kind = ELSOrderKind::AttackTarget;
			Attack.Target = EnemyCastle;
			Check(!Castle->Orders->SubmitOrder(Attack), TEXT("성의 기본 지정 공격 제거"));
			Check(Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, EnemyCastle->GetActorLocation()) == ELSWeaponCastResult::Ready,
			      TEXT("성의 수동 원형 포격"));
			Stage = 6;
		}
	}
	else if (Stage == 6)
	{
		ULSHealthComponent* EnemyHealth = EnemyCastle->FindComponentByClass<ULSHealthComponent>();
		FLSHealthSnapshot Snapshot;
		if (EnemyHealth->TryGetHealth(Snapshot) && Snapshot.CurrentHealth < Snapshot.MaxHealth)
		{
			Check(true, TEXT("성의 수동 무장 피해 적용"));
			EnemyHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10000, Castle, Castle));
			Check(IsValid(EnemyCastle) && EnemyCastle->GetCharacterMovement()->MovementMode == MOVE_None,
			      TEXT("파괴된 성은 보존하고 이동 차단"));
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Allies[Index]->SetActorLocation(FVector(-900, 700 + Index * 300, 96));
				Allies[Index]->Orders->SetComponentTickEnabled(true);
				Enemies[Index]->SetActorLocation(FVector(1700, -1500 + Index * 150, 96));
				Enemies[Index]->Orders->SetComponentTickEnabled(false);
				Enemies[Index]->BasicAttack->StopAttack();
			}
			Castle->SetActorLocation(FVector(-1200, 300, 144));
			ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
			Controller->ApplySelection({Castle, Allies[0], Allies[1], Allies[2]}, false, true);
			Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(0, 1000, 0), nullptr, false),
			      TEXT("다중 집결 이동 요청"));
			Stage = 7;
			StageTime = 0;
		}
	}
	else if (Stage == 7 && StageTime > 12)
	{
		for (ALSTroopUnit* Unit : Allies)
		{
			UE_LOG(LogTemp, Display, TEXT("HMH_GATHER %s distance=%.1f speed=%.1f"), *Unit->GetName(),
			       FVector::Dist2D(Unit->GetActorLocation(), FVector(0, 1000, 0)), Unit->GetVelocity().Size2D());
			Check(FVector::Dist2D(Unit->GetActorLocation(), FVector(0, 1000, 0)) < 350,
			      TEXT("병력이 클릭 지점 주변으로 실제 집결"));
		}
		Check(FVector::Dist2D(Castle->GetActorLocation(), FVector(0, 1000, 0)) < 350,
		      TEXT("성도 같은 집결 지점에 도착"));
		UE_LOG(LogTemp, Display, TEXT("HMH_GATHER castle location=%s distance=%.1f speed=%.1f order=%s"),
		       *Castle->GetActorLocation().ToString(), FVector::Dist2D(Castle->GetActorLocation(), FVector(0, 1000, 0)),
		       Castle->GetVelocity().Size2D(), *UEnum::GetValueAsString(Castle->Orders->GetOrderKind()));
		Finish();
	}
}
