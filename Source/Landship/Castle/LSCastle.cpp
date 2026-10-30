#include "Castle/LSCastle.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"

ALSCastle::ALSCastle()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
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
	Super::BeginPlay();
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

