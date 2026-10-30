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

	/** 소유 대상의 수치 읽기·구성 검사 이후 부모 BeginPlay 전에 한 번 호출한다. 수치 실패의 진단은 호출자가 먼저 수행해야 한다. */
	bool InitializeHealth(ELSCombatStatsInitResult StatsResult, float InitialHealth,
		const FLSPrepareCombatDeath& InPrepareDeathEvent, const FString& OwnerConfigurationError);

	/** 준비 전·실패 시 false이며 출력을 보존한다. 준비 성공은 체력 0인 사망 상태도 포함한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	bool TryGetHealth(FLSHealthSnapshot& OutHealth) const;

	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	ELSHealthInitializationState GetInitializationState() const { return InitializationState; }

	UFUNCTION(BlueprintPure, Category = "Landship|Health")
	FString GetInitializationFailureReason() const { return InitializationFailureReason; }

	/** 체력 변경의 공통 진입점이다. UE TakeDamage에 자동 연결되지 않는다. 알림 중 재진입해도 반환값은 이 요청의 결과다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Health")
	FLSApplyDamageResult ApplyDamage(const FLSDamageRequest& Request);

	/** 발사·공격 실행 시 생성해 명중 시 전달한다. 주체가 제거돼도 최초 진영을 보존한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Health")
	static FLSDamageRequest MakeDamageRequest(float DamageAmount, AActor* SourceActor, AActor* DamageCauser);

	/** 에디터와 런타임이 같은 제거 설정 검사 규칙을 사용한다. */
	FString GetConfigurationError() const;

	/** 비치명 피해에만 발생한다. 페이로드 체력은 해당 요청 직후 값이며 최신 상태와 다를 수 있다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Health")
	FLSOnCombatDamageApplied OnDamagedEvent;

	/** 필수 사망 처리 뒤 한 번 발생하고 반환 후 제거를 요청한다. 훅 도중 수신자가 제거되면 알림을 생략한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Health")
	FLSOnCombatDeath OnDiedEvent;

	/** 공개 사망 알림 뒤 소유 액터를 제거한다. 초기화 실패는 사망이 아니므로 제거하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Death")
	uint8 bDestroyOnDeath : 1 = false;

	/** 자동 제거가 켜졌을 때만 사용한다. 0은 알림 반환 직후 제거 요청, 양수는 수명 타이머 지연(초)이다. */
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
