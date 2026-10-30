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
#include "AIController.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

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

void ALSHMHRTSProbe::SetStage(int32 NewStage)
{
	Stage = NewStage;
	StageTime = 0;
}

void ALSHMHRTSProbe::HoldTestEnemy(ALSHMHRTSUnit* Unit)
{
	Unit->Selection->SetPlayerControlled(true);
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
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
		       Ally ? *Ally->GetActorLocation().ToString() : TEXT("none"),
		       Ally ? *UEnum::GetValueAsString(Ally->Orders->GetOrderKind()) : TEXT("none"),
		       Ally ? Ally->Orders->GetQueuedCount() : -1);
		Check(false, TEXT("전체 제한 시간"));
		Finish();
		return;
	}
	if (Stage == 0 && StageTime > 1.5f)
	{
		for (TActorIterator<ALSHMHRTSUnit> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("RTSAlly0")))
			{
				Ally = *It;
			}
			else if (It->ActorHasTag(TEXT("RTSEnemy0")))
			{
				Enemy = *It;
			}
			else if (It->ActorHasTag(TEXT("RTSEnemy1")))
			{
				SecondEnemy = *It;
			}
			else
			{
				It->SetActorLocation(FVector(-1600, 1200 + (It->GetUniqueID() % 4) * 150, 96));
				FLSOrder Hold;
				Hold.Kind = ELSOrderKind::Hold;
				It->Orders->SubmitOrder(Hold);
			}
		}
		Check(Ally && Enemy && SecondEnemy, TEXT("HMH 테스트 병력 연결"));
		if (!Ally || !Enemy || !SecondEnemy)
		{
			Finish();
			return;
		}
		Ally->SetActorLocation(FVector(-900, -600, 96));
		Enemy->SetActorLocation(FVector(800, -600, 96));
		SecondEnemy->SetActorLocation(FVector(1700, 1100, 96));
		HoldTestEnemy(Enemy);
		HoldTestEnemy(SecondEnemy);
		ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
		Check(Controller != nullptr, TEXT("플레이어 컨트롤러"));
		if (Controller)
		{
			Controller->ApplySelection({Ally});
			Controller->UseControlGroup(1, true);
			Controller->ApplySelection({Enemy});
			Check(!Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector::ZeroVector, nullptr, false),
			      TEXT("적 정보 선택은 명령 거부"));
			Controller->UseControlGroup(1, false);
			Check(Controller->GetSelectedActors().Contains(Ally), TEXT("부대 선택 복원"));
			if (ALSRTSCameraPawn* Camera = Cast<ALSRTSCameraPawn>(Controller->GetPawn()))
			{
				Camera->FocusLocation(FVector(1500, 1500, 0));
				Controller->UseControlGroup(1, false);
				Check(FVector::Dist2D(Camera->GetActorLocation(), Ally->GetActorLocation()) < 1,
				      TEXT("부대 두 번 선택 시 화면 복귀"));
			}
		}
		PreviousHealth = Enemy->TestHealth;
		FLSOrder Move;
		Move.Kind = ELSOrderKind::Move;
		Move.Destination = FVector(1100, -600, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("일반 이동 요청"));
		Move.Destination = FVector(1100, 300, 0);
		Check(Ally->Orders->SubmitOrder(Move, true), TEXT("첫 경유지 예약"));
		Move.Destination = FVector(650, 600, 0);
		Check(Ally->Orders->SubmitOrder(Move, true), TEXT("두 번째 경유지 예약"));
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
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Enemy;
		Check(Ally->Orders->SubmitOrder(Attack), TEXT("사거리 밖 지정 공격 요청"));
		Position = Ally->GetActorLocation();
		SetStage(2);
	}
	else if (Stage == 2 && Enemy->TestHealth < PreviousHealth)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) > 100, TEXT("지정 대상에게 접근 후 공격"));
		PreviousHealth = Enemy->TestHealth;
		FLSOrder Move;
		Move.Kind = ELSOrderKind::Move;
		Move.Destination = FVector(650, 1100, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("새 이동 명령으로 공격 교체"));
		Check(Ally->BasicAttack->GetAttackTarget() == nullptr, TEXT("새 이동 명령 시 공격 즉시 중단"));
		SetStage(3);
	}
	else if (Stage == 3 && StageTime > 0.3f)
	{
		Check(Enemy->TestHealth == PreviousHealth, TEXT("교체 이후 추가 피해 없음"));
		FLSOrder Hold;
		Hold.Kind = ELSOrderKind::Hold;
		Ally->Orders->SubmitOrder(Hold);
		Position = Ally->GetActorLocation();
		Enemy->SetActorLocation(Position + FVector(400, 0, 0));
		PreviousHealth = Enemy->TestHealth;
		SetStage(4);
	}
	else if (Stage == 4 && StageTime > 0.5f)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) < 5,
		      TEXT("위치 사수는 사거리 밖 적을 추적하지 않음"));
		Check(Enemy->TestHealth == PreviousHealth, TEXT("위치 사수 사거리 제한"));
		Enemy->SetActorLocation(Position + FVector(180, 0, 0));
		SetStage(5);
	}
	else if (Stage == 5 && Enemy->TestHealth < PreviousHealth)
	{
		Check(FVector::Dist2D(Position, Ally->GetActorLocation()) < 5, TEXT("위치 사수는 이동 없이 사거리 안 적 공격"));
		Enemy->TestHealth = 0;
		Enemy->SetActorEnableCollision(false);
		Ally->TestHealth = 200;
		SecondEnemy->SetActorLocation(Position + FVector(350, 0, 0));
		SecondEnemy->TestHealth = 20;
		FinalGoal = Position + FVector(900, 300, 0);
		FLSOrder Move;
		Move.Kind = ELSOrderKind::AttackMove;
		Move.Destination = Position + FVector(900, 0, 0);
		Check(Ally->Orders->SubmitOrder(Move), TEXT("공격 이동 요청"));
		Move.Kind = ELSOrderKind::Move;
		Move.Destination = FinalGoal;
		Check(Ally->Orders->SubmitOrder(Move, true), TEXT("교전 뒤 이동 예약"));
		SetStage(6);
	}
	else if (Stage == 6 && Ally->Orders->GetOrderKind() == ELSOrderKind::Idle && Ally->Orders->GetQueuedCount() == 0)
	{
		Check(SecondEnemy->TestHealth <= 0, TEXT("공격 이동 도중 적 처치"));
		Check(FVector::Dist2D(Ally->GetActorLocation(), FinalGoal) < 130,
		      TEXT("교전 종료 후 원래 목적지와 예약 목적지까지 이동"));
		AActor* Building = GetWorld()->SpawnActor<AActor>();
		ULSSelectableComponent* BuildingSelection = NewObject<ULSSelectableComponent>(Building);
		Building->AddInstanceComponent(BuildingSelection);
		BuildingSelection->Kind = ELSSelectionKind::Building;
		BuildingSelection->RegisterComponent();
		ULSOrderComponent* BuildingOrders = NewObject<ULSOrderComponent>(Building);
		Building->AddInstanceComponent(BuildingOrders);
		BuildingOrders->RegisterComponent();
		ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
		Controller->ApplySelection({Ally, Building});
		const FVector MixedGoal(0, 1000, 0);
		Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, MixedGoal, nullptr, false),
		      TEXT("건물 혼합 선택의 이동 요청"));
		Check(FVector::Dist2D(Ally->Orders->GetOrderDestination(), MixedGoal) < 5,
		      TEXT("이동 불가 건물은 대형 중심 계산에서 제외"));
		Check(BuildingOrders->GetOrderKind() == ELSOrderKind::Idle, TEXT("건물에는 이동 명령을 전달하지 않음"));
		CastChecked<AAIController>(Ally->GetController())->StopMovement();
		Check(!Ally->Orders->DidLastMovementSucceed(), TEXT("외부 이동 중단을 도착 성공으로 처리하지 않음"));
		SetStage(7);
	}
	else if (Stage == 7 && StageTime > 0.2f)
	{
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::Idle, TEXT("중단된 이동 명령 종료"));
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		bool bFoundBoundary = false;
		for (int32 X = -20000; X <= 20000 && !bFoundBoundary; X += 25)
		{
			const FVector RawGoal(X, -400, 0);
			FNavLocation Projected;
			if (!Navigation || !Navigation->ProjectPointToNavigation(RawGoal, Projected, FVector(150, 150, 300)))
			{
				continue;
			}
			const double Offset = FVector::Dist2D(RawGoal, Projected.Location);
			if (Offset < 100 || Offset > 145)
			{
				continue;
			}
			Ally->SetActorLocation(Projected.Location + FVector(0, 0, 96));
			FLSOrder BoundaryOrder;
			BoundaryOrder.Kind = ELSOrderKind::AttackMove;
			BoundaryOrder.Destination = RawGoal;
			if (!Ally->Orders->SubmitOrder(BoundaryOrder))
			{
				UE_LOG(LogTemp, Display, TEXT("HMH_BOUNDARY rejected raw=%s projected=%s agent=%s"), *RawGoal.ToString(), *Projected.Location.ToString(), *Ally->GetNavAgentLocation().ToString());
				continue;
			}
			bFoundBoundary = true;
			Check(FVector::Dist2D(Ally->Orders->GetOrderDestination(), Projected.Location) < 5,
			      TEXT("공격 이동은 클릭 좌표 대신 NavMesh 보정 좌표를 저장"));
		}
		Check(bFoundBoundary, TEXT("NavMesh 바깥 경계 재현 좌표 확보"));
		if (!bFoundBoundary)
		{
			Finish();
			return;
		}
		SetStage(8);
	}
	else if (Stage == 8 && StageTime > 0.9f)
	{
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::Idle,
		      TEXT("보정된 경계 도착 후 공격 이동 요청을 반복하지 않음"));
		Check(Ally->Orders->DidLastMovementSucceed(), TEXT("보정 목적지의 AlreadyAtGoal을 성공으로 구분"));
		FLSOrder MoveOrder;
		MoveOrder.Kind = ELSOrderKind::Move;
		MoveOrder.Destination = FVector(500, 1000, 0);
		Check(Ally->Orders->SubmitOrder(MoveOrder), TEXT("이전 요청 ID 재현용 이동"));
		UPathFollowingComponent* PathFollowing = CastChecked<AAIController>(Ally->GetController())->GetPathFollowingComponent();
		const FAIRequestID OldRequestId = PathFollowing->GetCurrentRequestId();
		MoveOrder.Destination = FVector(800, 1000, 0);
		Check(Ally->Orders->SubmitOrder(MoveOrder), TEXT("새 이동 요청으로 교체"));
		PathFollowing->OnRequestFinished.Broadcast(OldRequestId, FPathFollowingResult(EPathFollowingResult::Success, 0));
		SetStage(9);
	}
	else if (Stage == 9 && StageTime > 0.15f)
	{
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::Move, TEXT("예전 이동 완료 이벤트가 새 명령을 종료하지 않음"));
		// 길 막힘 결과를 경로 추적 컴포넌트에 주입해 실패 처리 경로를 재현한다.
		UPathFollowingComponent* PathFollowing = CastChecked<AAIController>(Ally->GetController())->GetPathFollowingComponent();
		PathFollowing->OnPathFinished(FPathFollowingResult(EPathFollowingResult::Blocked, 0));
		Check(!Ally->Orders->DidLastMovementSucceed(), TEXT("Blocked 결과를 도착 성공과 구분"));
		SetStage(10);
	}
	else if (Stage == 10 && StageTime > 0.2f)
	{
		Check(Ally->Orders->GetOrderKind() == ELSOrderKind::Idle, TEXT("실패한 이동 명령 종료"));
		Finish();
	}
}
