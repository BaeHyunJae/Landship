#include "Controls/Tests/LSHMHRTSProbe.h"
#include "Controls/Tests/LSHMHRTSUnit.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

ALSHMHRTSProbe::ALSHMHRTSProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSHMHRTSProbe::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(FParse::Param(FCommandLine::Get(), TEXT("HMHRTSTestExit")));
}

void ALSHMHRTSProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("HMH_RTS %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Message);
}

void ALSHMHRTSProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_RTS_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSHMHRTSProbe::SetStage(int32 NewStage) { Stage = NewStage; StageTime = 0; }

void ALSHMHRTSProbe::HoldTestEnemy(ALSHMHRTSUnit* Unit)
{
	Unit->Selection->SetPlayerControlled(true);
	FLSOrder Hold; Hold.Kind = ELSOrderKind::Hold;
	Unit->Orders->SubmitOrder(Hold);
	Unit->Selection->SetPlayerControlled(false);
}

void ALSHMHRTSProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 100)
	{
		UE_LOG(LogTemp, Display, TEXT("HMH_RTS timeout stage=%d location=%s order=%s queue=%d"), Stage,
			Ally ? *Ally->GetActorLocation().ToString() : TEXT("none"), Ally ? *UEnum::GetValueAsString(Ally->Orders->GetOrderKind()) : TEXT("none"), Ally ? Ally->Orders->GetQueuedCount() : -1);
		Check(false, TEXT("전체 제한 시간")); Finish(); return;
	}
	if (Stage == 0 && StageTime > 1.5f)
	{
		for (TActorIterator<ALSHMHRTSUnit> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("RTSAlly0"))) { Ally = *It; }
			else if (It->ActorHasTag(TEXT("RTSEnemy0"))) { Enemy = *It; }
			else if (It->ActorHasTag(TEXT("RTSEnemy1"))) { SecondEnemy = *It; }
			else
			{
				It->SetActorLocation(FVector(-1600, 1200 + (It->GetUniqueID() % 4) * 150, 96));
				FLSOrder Hold; Hold.Kind = ELSOrderKind::Hold; It->Orders->SubmitOrder(Hold);
			}
		}
		Check(Ally && Enemy && SecondEnemy, TEXT("HMH 테스트 병력 연결"));
		if (!Ally || !Enemy || !SecondEnemy) { Finish(); return; }
		Ally->SetActorLocation(FVector(-900, -600, 96));
		Enemy->SetActorLocation(FVector(800, -600, 96));
		SecondEnemy->SetActorLocation(FVector(1700, 1100, 96));
		HoldTestEnemy(Enemy); HoldTestEnemy(SecondEnemy);
		ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
		Check(Controller != nullptr, TEXT("플레이어 컨트롤러"));
		if (Controller)
		{
			Controller->ApplySelection({ Ally }); Controller->UseControlGroup(1, true);
			Controller->ApplySelection({ Enemy });
			Check(!Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector::ZeroVector, nullptr, false), TEXT("적 정보 선택은 명령 거부"));
			Controller->UseControlGroup(1, false);
			Check(Controller->GetSelectedActors().Contains(Ally), TEXT("부대 선택 복원"));
			if (ALSRTSCameraPawn* Camera = Cast<ALSRTSCameraPawn>(Controller->GetPawn()))
			{
				Camera->FocusLocation(FVector(1500, 1500, 0));
				Controller->UseControlGroup(1, false);
				Check(FVector::Dist2D(Camera->GetActorLocation(), Ally->GetActorLocation()) < 1, TEXT("부대 두 번 선택 시 화면 복귀"));
			}
		}
		PreviousHealth = Enemy->TestHealth;
		FLSOrder Move; Move.Kind = ELSOrderKind::Move; Move.Destination = FVector(1100, -600, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("일반 이동 요청"));
		Move.Destination = FVector(1100, 300, 0); Check(Ally->Orders->SubmitOrder(Move, true), TEXT("첫 경유지 예약"));
		Move.Destination = FVector(650, 600, 0); Check(Ally->Orders->SubmitOrder(Move, true), TEXT("두 번째 경유지 예약"));
		FinalGoal = Move.Destination;
		Check(Ally->Orders->GetQueuedCount() == 2, TEXT("이동 예약 수"));
		SetStage(1);
	}
	else if (Stage == 1 && Ally->Orders->GetOrderKind() == ELSOrderKind::Idle && Ally->Orders->GetQueuedCount() == 0)
	{
		Check(FVector::Dist2D(Ally->GetActorLocation(), FinalGoal) < 130, TEXT("장애물 우회와 예약 경유지 도착"));
		Check(Enemy->TestHealth == PreviousHealth, TEXT("일반 이동 중 공격하지 않음"));
		Ally->TestHealth = 200;
		Enemy->SetActorLocation(Ally->GetActorLocation() + FVector(600, 0, 0));
		FLSOrder Attack; Attack.Kind = ELSOrderKind::AttackTarget; Attack.Target = Enemy;
		Check(Ally->Orders->SubmitOrder(Attack), TEXT("사거리 밖 지정 공격 요청"));
		Position = Ally->GetActorLocation();
		SetStage(2);
	}
	else if (Stage == 2 && Enemy->TestHealth < PreviousHealth)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) > 100, TEXT("지정 대상에게 접근 후 공격"));
		PreviousHealth = Enemy->TestHealth;
		FLSOrder Move; Move.Kind = ELSOrderKind::Move; Move.Destination = FVector(650, 1100, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("새 이동 명령으로 공격 교체"));
		Check(Ally->BasicAttack->GetAttackTarget() == nullptr, TEXT("새 이동 명령 시 공격 즉시 중단"));
		SetStage(3);
	}
	else if (Stage == 3 && StageTime > 0.3f)
	{
		Check(Enemy->TestHealth == PreviousHealth, TEXT("교체 이후 추가 피해 없음"));
		FLSOrder Hold; Hold.Kind = ELSOrderKind::Hold; Ally->Orders->SubmitOrder(Hold);
		Position = Ally->GetActorLocation();
		Enemy->SetActorLocation(Position + FVector(400, 0, 0));
		PreviousHealth = Enemy->TestHealth;
		SetStage(4);
	}
	else if (Stage == 4 && StageTime > 0.5f)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) < 5, TEXT("위치 사수는 사거리 밖 적을 추적하지 않음"));
		Check(Enemy->TestHealth == PreviousHealth, TEXT("위치 사수 사거리 제한"));
		Enemy->SetActorLocation(Position + FVector(180, 0, 0));
		SetStage(5);
	}
	else if (Stage == 5 && Enemy->TestHealth < PreviousHealth)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) < 5, TEXT("위치 사수는 이동 없이 사거리 안 적 공격"));
		Enemy->TestHealth = 0; Enemy->SetActorEnableCollision(false);
		Ally->TestHealth = 200;
		SecondEnemy->SetActorLocation(Position + FVector(350, 0, 0));
		SecondEnemy->TestHealth = 20;
		FinalGoal = Position + FVector(900, 300, 0);
		FLSOrder Move; Move.Kind = ELSOrderKind::AttackMove; Move.Destination = Position + FVector(900, 0, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("공격 이동 요청"));
		Move.Kind = ELSOrderKind::Move; Move.Destination = FinalGoal;
		Check(Ally->Orders->SubmitOrder(Move, true), TEXT("교전 뒤 이동 예약"));
		SetStage(6);
	}
	else if (Stage == 6 && Ally->Orders->GetOrderKind() == ELSOrderKind::Idle && Ally->Orders->GetQueuedCount() == 0)
	{
		Check(SecondEnemy->TestHealth <= 0, TEXT("공격 이동 도중 적 처치"));
		Check(FVector::Dist2D(Ally->GetActorLocation(), FinalGoal) < 130, TEXT("교전 종료 후 원래 목적지와 예약 목적지까지 이동"));
		Finish();
	}
}
