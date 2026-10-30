#pragma once

#include "CoreMinimal.h"
#include "Combat/LSFaction.h"
#include "LSHealthTypes.generated.h"

UENUM(BlueprintType)
enum class ELSHealthInitializationState : uint8
{
	NotAttempted,
	Ready,
	Failed
};

UENUM(BlueprintType)
enum class ELSApplyDamageStatus : uint8
{
	Applied,
	NoEffectZeroDamage,
	RejectedDead,
	RejectedSelfDamageDisabled,
	RejectedNotReady,
	RejectedInitializationFailed,
	/** 수신자/컴포넌트가 제거 중이거나 월드가 종료 중인 정상 수명 거부다. */
	RejectedInvalidTarget,
	RejectedInvalidInput,
	RejectedFriendlyFireDisabled
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSHealthSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	uint8 bIsDead : 1 = false;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSDamageRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float DamageAmount = 0.0f;

	/** 공격한 유닛·성·거점 자체다. 무기·발사체는 DamageCauser에 넣는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<AActor> SourceActor = nullptr;

	/** 발사체·무기·환경 등 직접 피해 원인이다. 없는 경우도 허용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TObjectPtr<AActor> DamageCauser = nullptr;

	/** 지연 공격은 요청 생성 시 보관한 진영으로 주체 제거 뒤에도 오폭을 판정한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	uint8 bHasSourceFaction : 1 = false;

	/** 보관 여부가 참일 때만 의미가 있다. SourceActor를 바꿔도 갱신되지 않으며, 직접 요청은 유효한 주체의 진영을 조회한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	ELSFaction SourceFaction = ELSFaction::Ally;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSApplyDamageResult
{
	GENERATED_BODY()

	FLSApplyDamageResult() = default;
	FLSApplyDamageResult(ELSApplyDamageStatus InStatus, float InAppliedDamage)
		: Status(InStatus), AppliedDamage(InAppliedDamage) {}

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	ELSApplyDamageStatus Status = ELSApplyDamageStatus::RejectedNotReady;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float AppliedDamage = 0.0f;
};

/** 적용된 요청의 결과다. 이후 재진입으로 바뀐 현재 상태와 구분한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float RequestedDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float AppliedDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	float CurrentHealth = 0.0f;

	/** 피해 시점에 이미 제거된 주체는 nullptr이다. 알림 중에도 제거될 수 있으므로 사용 전 유효성을 확인한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AActor> DamageCauser = nullptr;
};
