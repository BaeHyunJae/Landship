#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/LSCombatStats.h"
#include "Combat/LSHealthTypes.h"
#include "LSHealthComponent.generated.h"

DECLARE_DELEGATE(FLSPrepareCombatDeath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSOnCombatDamageApplied, const FLSDamageResult&, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSOnCombatDeath, const FLSDamageResult&, Damage);

/** 세 대상이 공유하는 체력 상태와 피해 처리다. 대상별 사망 처리는 네이티브 훅에 맡긴다. */
UCLASS(ClassGroup = (Landship))
class LANDSHIP_API ULSHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULSHealthComponent();

	/** 소유 대상의 수치 읽기·구성 검사 이후 부모 BeginPlay 전에 한 번 호출한다. */
	bool InitializeHealth(ELSCombatStatsInitResult StatsResult, float InitialHealth,
		const FLSPrepareCombatDeath& InPrepareDeathEvent, const FString& OwnerConfigurationError);

	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	bool TryGetHealth(FLSHealthSnapshot& OutHealth) const;

	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	ELSHealthInitializationState GetInitializationState() const { return InitializationState; }

	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	FString GetInitializationFailureReason() const { return InitializationFailureReason; }

	UFUNCTION(BlueprintCallable, Category = "Landship|Health")
	FLSApplyDamageResult ApplyDamage(const FLSDamageRequest& Request);

	/** 에디터와 런타임이 같은 제거 설정 검사 규칙을 사용한다. */
	FString GetConfigurationError() const;

	UPROPERTY(BlueprintAssignable, Category = "Landship|Health")
	FLSOnCombatDamageApplied OnDamagedEvent;

	UPROPERTY(BlueprintAssignable, Category = "Landship|Health")
	FLSOnCombatDeath OnDiedEvent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Death")
	uint8 bDestroyOnDeath : 1 = false;

	/** 0이면 사망 알림 반환 직후 제거를 요청한다. 단위는 초다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Death", meta = (ClampMin = "0", Units = "s"))
	float DestroyDelay = 10.0f;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	bool FailInitialization(const FString& Reason);

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Health")
	ELSHealthInitializationState InitializationState = ELSHealthInitializationState::NotAttempted;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Health")
	FString InitializationFailureReason;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Health")
	FLSHealthSnapshot Health;

	FLSPrepareCombatDeath PrepareDeathEvent;
};
