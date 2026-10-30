#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LSCombatStatRows.generated.h"

/** 병력과 적 분대가 공유하는 초기 수치 형식이다. 현재 체력은 후속 체력 기능에서 관리한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSUnitStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 초기 체력. 0보다 커야 한다. */
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

	/** 이동 속도(cm/s). 이번 초기화에서는 저장만 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm/s"))
	float MovementSpeed = 0.0f;
};

/** 성의 초기 수치다. 무장별 공격 수치는 성의 수치에 포함하지 않는다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCastleStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 초기 체력. 0보다 커야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float Health = 0.0f;

	/** 시야 반경(cm). 0 이상이어야 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm"))
	float VisionRadius = 0.0f;

	/** 이동 속도(cm/s). 이번 초기화에서는 저장만 한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats", meta = (Units = "cm/s"))
	float MovementSpeed = 0.0f;
};

/** 고정된 적 거점의 초기 수치다. 감지에는 공격 사거리를 사용한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSOutpostStatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 초기 체력. 0보다 커야 한다. */
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
