#include "Castle/Tests/LSHMHMovementCastle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

ALSHMHMovementCastle::ALSHMHMovementCastle()
{
	PrimaryActorTick.bCanEverTick = true;
	UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestCastleMesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
	Mesh->SetRelativeScale3D(FVector(2.0f));
}

void ALSHMHMovementCastle::BeginPlay()
{
	// 개인 테스트 맵에서만 유효한 수치를 공급하여 공용 데이터 에셋을 수정하지 않는다.
	UDataTable* Table = NewObject<UDataTable>(this);
	Table->RowStruct = FLSCastleStatRow::StaticStruct();
	FLSCastleStatRow Row;
	Row.Health = 100.0f;
	Row.VisionRadius = 1500.0f;
	Row.MovementSpeed = 400.0f;
	Table->AddRow(TEXT("MovementTest"), Row);
	StatsRow.DataTable = Table;
	StatsRow.RowName = TEXT("MovementTest");
	Super::BeginPlay();
	if (UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		if (ANavigationData* Data = Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create))
		{
			FNavDataConfig Config = Data->GetConfig();
			Config.AgentRadius = 142.0f;
			Config.AgentHeight = 200.0f;
			Data->SetConfig(Config);
			Data->RebuildAll();
		}
	}
	Start = GetActorLocation();
	bAutomatic = FParse::Param(FCommandLine::Get(), TEXT("HMHMovementTestExit"));
	SetActorTickEnabled(bAutomatic);
}

void ALSHMHMovementCastle::Check(bool bPassed, const TCHAR* Description)
{
	if (!bPassed)
	{
		++Failures;
	}
	UE_LOG(LogTemp, Display, TEXT("HMH_MOVE %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Description);
}

void ALSHMHMovementCastle::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_MOVE_FINISHED failures=%d"), Failures);
	FPlatformMisc::RequestExitWithStatus(false, Failures == 0 ? 0 : 1);
	SetActorTickEnabled(false);
}

void ALSHMHMovementCastle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 30.0f)
	{
		Check(false, TEXT("이동 시험 제한 시간 초과"));
		Finish();
		return;
	}
	if (Stage == 0 && StageTime > 1.0f)
	{
		Check(!IsMoving(), TEXT("시작 시 정지"));
		Check(Cast<UFloatingPawnMovement>(GetMovementComponent())->MaxSpeed == 400.0f, TEXT("행의 이동 속도 반영"));
		Check(RequestMove(Start + FVector(1400, 0, -100)), TEXT("최초 경로 요청"));
		Check(IsMoving(), TEXT("요청 후 이동 상태"));
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 0.6f)
	{
		Check(FVector::Dist2D(Start, GetActorLocation()) > 50.0f, TEXT("실제 위치 변화"));
		Check(IsMoving(), TEXT("이동 도중 상태 조회"));
		Goal = Start + FVector(1400, 600, -100);
		Check(RequestMove(Goal), TEXT("이동 도중 목적지 변경"));
		Check(!RequestMove(FVector(1.e8)), TEXT("맵 밖 명령 거부"));
		Check(IsMoving(), TEXT("잘못된 명령 뒤 기존 이동 유지"));
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && !IsMoving())
	{
		UE_LOG(LogTemp, Display, TEXT("HMH_MOVE arrival=%s goal=%s"), *GetActorLocation().ToString(), *Goal.ToString());
		Check(FVector::Dist2D(GetActorLocation(), Goal) < 180.0f, TEXT("장애물 우회 후 변경된 목적지 도착"));
		Check(RequestMove(Start - FVector(0, 0, 100)), TEXT("복귀 명령"));
		StopMoving();
		Check(!IsMoving() && GetVelocity().IsNearlyZero(), TEXT("명시적 정지"));
		Finish();
	}
}
