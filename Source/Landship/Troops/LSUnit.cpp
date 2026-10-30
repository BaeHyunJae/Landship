#include "Troops/LSUnit.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/CapsuleComponent.h"
#include "Logging/StructuredLog.h"

ALSUnit::ALSUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCapsuleComponent()->SetCollisionObjectType(LSCollisionChannels::Unit);
}

void ALSUnit::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} initialized: Health={Health} AttackPower={AttackPower} AttackInterval={AttackInterval} AttackRange={AttackRange} DetectionRange={DetectionRange} VisionRadius={VisionRadius} MovementSpeed={MovementSpeed}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("AttackPower", Stats.AttackPower),
			("AttackInterval", Stats.AttackInterval),
			("AttackRange", Stats.AttackRange),
			("DetectionRange", Stats.DetectionRange),
			("VisionRadius", Stats.VisionRadius),
			("MovementSpeed", Stats.MovementSpeed));
	}
	Super::BeginPlay();
}

bool ALSUnit::TryGetStats(FLSUnitStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}

