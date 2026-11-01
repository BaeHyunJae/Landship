#include "Match/Tests/LSBattleOutcomeTestObserver.h"

#include "Combat/LSHealthComponent.h"
#include "Engine/DataTable.h"
#include "Controls/LSPlayerController.h"

bool ULSBattleOutcomeTestLibrary::TryCastleWeapon(APlayerController* Controller, const FVector& Aim)
{
	ALSPlayerController* Player = Cast<ALSPlayerController>(Controller);
	return Player && Player->ArmCastleWeapon(ELSCastleWeapon::Cannon) &&
		Player->ConfirmCastleWeapon(Aim) == ELSWeaponCastResult::Ready;
}

ALSBattleOutcomeTestCastle::ALSBattleOutcomeTestCastle()
{
	UDataTable* Table = CreateDefaultSubobject<UDataTable>(TEXT("CastleStats"));
	Table->RowStruct = FLSCastleStatRow::StaticStruct();
	FLSCastleStatRow Row;
	Row.Health = 100;
	Row.VisionRadius = 1000;
	Row.MovementSpeed = 300;
	Table->AddRow(TEXT("Test"), Row);
	StatsRow.DataTable = Table;
	StatsRow.RowName = TEXT("Test");
	Health->bDestroyOnDeath = false;
}

void ALSBattleOutcomeTestCastle::BeginPlay()
{
	if (!bDestroyDuringDeath && !bLeaveHealthUnready)
	{
		Super::BeginPlay();
		return;
	}
	// 공개 사망 알림 전에 소유자가 사라지는 필수 훅과 미준비 체력을 별도로 검사한다.
	if (!bLeaveHealthUnready)
	{
		Health->InitializeHealth(ELSCombatStatsInitResult::Success, 100,
			FLSPrepareCombatDeath::CreateLambda([this] { Destroy(); }), FString());
	}
	APawn::BeginPlay();
}

void ULSBattleOutcomeTestObserver::OnFinalized(ELSBattleOutcome Outcome)
{
	Outcomes.Add(Outcome);
	FinalizedFrame = GFrameCounter;
	bSawStoppedAttackAtFinalization = AttackToInspect.IsValid() && !AttackToInspect->GetAttackTarget();
}

void ULSBattleOutcomeTestObserver::OnExecuted(const FLSAttackExecution& Execution)
{
	ExecutionSources.Add(Execution.Source);
	ExecutionFrames.Add(GFrameCounter);
}

void ULSBattleOutcomeTestObserver::OnCastleDied(const FLSDamageResult& Damage)
{
	++PublicCastleDeaths;
}
