#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/LSCombatStatRows.h"
#include "Combat/LSCombatStats.h"
#include "LSUnit.generated.h"

/** 전투 동작을 붙이기 전에 대상의 초기 수치를 준비한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSUnit : public ACharacter
{
	GENERATED_BODY()

public:
	ALSUnit();

	/** 초기화 전이나 실패 시에는 출력값을 변경하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool TryGetStats(FLSUnitStatRow& OutStats) const;

	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool IsStatsInitialized() const { return StatsInitResult == ELSCombatStatsInitResult::Success; }

	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	ELSCombatStatsInitResult GetStatsInitResult() const { return StatsInitResult; }

protected:
	virtual void BeginPlay() override;

	/** 수치 자체는 테이블에서 조정하고 여기서는 사용할 행만 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Stats", meta = (RowType = "/Script/Landship.LSUnitStatRow"))
	FDataTableRowHandle StatsRow;

private:
	/** 현재 전투 상태와 구분되는 초기 수치 복사본이다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	FLSUnitStatRow Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	ELSCombatStatsInitResult StatsInitResult = ELSCombatStatsInitResult::NotAttempted;
};

