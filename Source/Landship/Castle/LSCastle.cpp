#include "Castle/LSCastle.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"
#include "AIController.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"

ALSCastle::ALSCastle()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
	Collision->SetCanEverAffectNavigation(false);
	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->NavAgentProps.AgentRadius = 142.0f;
	Movement->NavAgentProps.AgentHeight = 200.0f;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Selection->Kind = ELSSelectionKind::Castle;
	Orders = CreateDefaultSubobject<ULSOrderComponent>(TEXT("Orders"));
}

void ALSCastle::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} initialized: Health={Health} VisionRadius={VisionRadius} MovementSpeed={MovementSpeed}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("VisionRadius", Stats.VisionRadius),
			("MovementSpeed", Stats.MovementSpeed));
	}
	Movement->MaxSpeed = IsStatsInitialized() ? Stats.MovementSpeed : 0.0f;
	Super::BeginPlay();
}

UPawnMovementComponent* ALSCastle::GetMovementComponent() const
{
	return Movement;
}

bool ALSCastle::RequestMove(const FVector& Destination)
{
	AAIController* AI = Cast<AAIController>(GetController());
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Destination.ContainsNaN() || !AI || !Navigation || !IsStatsInitialized() || Movement->MaxSpeed <= 0.0f)
	{
		return false;
	}
	FNavLocation Goal;
	if (!Navigation->ProjectPointToNavigation(Destination, Goal, FVector(100.0f, 100.0f, 250.0f)))
	{
		return false;
	}
	// 도달 불가능한 명령이 진행 중인 이동을 취소하지 않도록 먼저 경로를 검사한다.
	UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(GetWorld(), GetNavAgentLocation(), Goal.Location, this);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return false;
	}
	return AI->MoveToLocation(Goal.Location, AcceptanceRadius, true, true, false, false, nullptr, false)
		!= EPathFollowingRequestResult::Failed;
}

bool ALSCastle::IsMoving() const
{
	const AAIController* AI = Cast<AAIController>(GetController());
	return AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
}

void ALSCastle::StopMoving()
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	Movement->StopMovementImmediately();
}

bool ALSCastle::TryGetStats(FLSCastleStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}

