#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSCombatStatRows.h"
#include "Combat/LSCombatStats.h"
#include "LSOutpost.generated.h"

class UBoxComponent;

/** 전투 동작을 붙이기 전에 대상의 초기 수치를 준비한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSOutpost : public AActor
{
	GENERATED_BODY()

public:
	ALSOutpost();

	/** 초기화 전이나 실패 시에는 출력값을 변경하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool TryGetStats(FLSOutpostStatRow& OutStats) const;

	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool IsStatsInitialized() const { return StatsInitResult == ELSCombatStatsInitResult::Success; }

	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	ELSCombatStatsInitResult GetStatsInitResult() const { return StatsInitResult; }

protected:
	virtual void BeginPlay() override;

	/** 수치 자체는 테이블에서 조정하고 여기서는 사용할 행만 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Stats", meta = (RowType = "/Script/Landship.LSOutpostStatRow"))
	FDataTableRowHandle StatsRow;

	/** 전투 대상 탐색에 사용하는 건물 충돌 영역이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<UBoxComponent> Collision;

private:
	/** 현재 전투 상태와 구분되는 초기 수치 복사본이다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	FLSOutpostStatRow Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	ELSCombatStatsInitResult StatsInitResult = ELSCombatStatsInitResult::NotAttempted;
};

