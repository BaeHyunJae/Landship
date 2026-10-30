#pragma once

#include "CoreMinimal.h"
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
	RejectedInvalidTarget,
	RejectedInvalidInput
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

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	TObjectPtr<AActor> DamageCauser = nullptr;
};
