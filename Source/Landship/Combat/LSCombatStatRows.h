#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LSCombatStatRows.generated.h"

/** 병력과 적 분대가 공유하는 초기 수치다. 변경되는 현재 체력은 체력 컴포넌트가 관리한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSUnitStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 최대 체력과 시작 현재 체력에 쓰는 양수다. 이 행은 피해로 변경되지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float Health = 0.0f;

	/** 기본 공격 1회의 공격력. 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float AttackPower = 0.0f;

	/** 기본 공격 간격(초). 0보다 커야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "s"))
	float AttackInterval = 0.0f;

	/** 기본 공격 사거리(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float AttackRange = 0.0f;

	/** 대상 감지 범위(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float DetectionRange = 0.0f;

	/** 시야 반경(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float VisionRadius = 0.0f;

	/** 이동 속도(cm/s). 수치 조회용으로 저장하며 이동 컴포넌트에 자동 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm/s"))
	float MovementSpeed = 0.0f;
};

/** 성의 초기 수치다. 무장별 공격 수치는 성의 수치에 포함하지 않는다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCastleStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 최대 체력과 시작 현재 체력에 쓰는 양수다. 이 행은 피해로 변경되지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float Health = 0.0f;

	/** 시야 반경(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float VisionRadius = 0.0f;

	/** 이동 속도(cm/s). 수치 조회용으로 저장하며 이동 컴포넌트에 자동 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm/s"))
	float MovementSpeed = 0.0f;
};

/** 고정된 적 거점의 초기 수치다. 감지에는 공격 사거리를 사용한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSOutpostStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 최대 체력과 시작 현재 체력에 쓰는 양수다. 이 행은 피해로 변경되지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float Health = 0.0f;

	/** 기본 공격 1회의 공격력. 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float AttackPower = 0.0f;

	/** 기본 공격 간격(초). 0보다 커야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "s"))
	float AttackInterval = 0.0f;

	/** 기본 공격 사거리(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float AttackRange = 0.0f;
};
