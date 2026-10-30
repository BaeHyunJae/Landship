#include "Troops/LSUnit.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Logging/StructuredLog.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"

ALSUnit::ALSUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCapsuleComponent()->SetCollisionObjectType(LSCollisionChannels::Unit);
	// 기본 Pawn 프로필은 Visibility를 무시하므로 전장 클릭 선택용 적중을 명시한다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BasicAttack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("BasicAttack"));
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Orders = CreateDefaultSubobject<ULSOrderComponent>(TEXT("Orders"));
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ALSUnit::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (IsStatsInitialized())
	{
		FLSBasicAttackSettings AttackSettings;
		AttackSettings.Damage = Stats.AttackPower;
		AttackSettings.Interval = Stats.AttackInterval;
		AttackSettings.Range = Stats.AttackRange;
		BasicAttack->ConfigureAttack(AttackSettings);
		GetCharacterMovement()->MaxWalkSpeed = Stats.MovementSpeed;
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

