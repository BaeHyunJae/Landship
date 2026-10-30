#include "Enemies/LSOutpost.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"

ALSOutpost::ALSOutpost()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
}

void ALSOutpost::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} initialized: Health={Health} AttackPower={AttackPower} AttackInterval={AttackInterval} AttackRange={AttackRange}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("AttackPower", Stats.AttackPower),
			("AttackInterval", Stats.AttackInterval),
			("AttackRange", Stats.AttackRange));
	}
	Super::BeginPlay();
}

bool ALSOutpost::TryGetStats(FLSOutpostStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}

