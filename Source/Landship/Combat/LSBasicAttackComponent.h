#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/LSFaction.h"
#include "LSBasicAttackComponent.generated.h"

class ALSBasicProjectile;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ELSProjectileVisualStyle : uint8
{
	Arrow,
	TurretBolt,
	BallistaBolt
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSProjectileAttackSettings
{
	GENERATED_BODY()
	/** 비어 있으면 기존 즉시 피해 공격을 사용한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile")
	TSubclassOf<ALSBasicProjectile> ProjectileClass;
	/** 풀 수명 초기화를 지원하는 클래스만 재활용한다. 미지원 클래스는 기존 생성·제거를 유지한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile")
	bool bUsePool = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile", meta = (ClampMin = "1", Units = "cm/s"))
	float Speed = 1400.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile", meta = (ClampMin = "0.01", Units = "s"))
	float Lifetime = 3.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile", meta = (ClampMin = "0.1", Units = "cm"))
	float Radius = 6.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile")
	TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Projectile")
	ELSProjectileVisualStyle VisualStyle = ELSProjectileVisualStyle::Arrow;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSBasicAttackSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0"))
	float Damage = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0.001", Units = "s"))
	float Interval = 1.0f;
	/** 양측 충돌 원통 외곽 사이의 거리다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Attack", meta = (ClampMin = "0", Units = "cm"))
	float Range = 0.0f;
};

/** 피해 결과가 아닌 공격 확정 시점의 기록이다. 참조는 소비 시 다시 검증해야 한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSAttackExecution
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TWeakObjectPtr<AActor> Source;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	TWeakObjectPtr<AActor> Target;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	FVector SourceLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	FVector TargetLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	uint8 bProjectile : 1 = false;
	/** 이 공격 컴포넌트 안에서 증가한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	int64 ExecutionId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	double TimestampSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	bool bHasSourceFaction = false;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	ELSFaction SourceFaction = ELSFaction::Ally;
	UPROPERTY(BlueprintReadOnly, Category = "Attack")
	FVector Direction = FVector::ZeroVector;
};

DECLARE_DELEGATE_RetVal(AActor*, FLSResolveAttackTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSAttackExecuted, const FLSAttackExecution&, Execution);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSProjectileLaunched, ALSBasicProjectile*, Projectile);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSBasicAttackPerformed, AActor*, Target, float, AppliedDamage);

/** 공격 간격과 즉시 피해 또는 투사체 발사를 담당한다. 추적 이동은 별도다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSBasicAttackComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSBasicAttackComponent();
	bool IsResolvingAttack() const { return bResolvingAttack; }
	uint64 GetCombatLifeSerial() const { return CombatLifeSerial; }
	/** 풀에 보관한 소유자의 표적·쿨타임·배율·외부 구독을 정리한다. 타격 실행 중에는 거부한다. */
	bool ResetForPool();
	/** 명령 컴포넌트가 전투를 제어하는 액터는 생성자에서 자동 시작을 끈다. */
	void SetAutoAttackOnBeginPlay(bool bEnabled)
	{
		bAutoAttackOnBeginPlay = bEnabled;
	}
	virtual void Deactivate() override;
	/** 준비된 타격 직전에 대상을 새로 정한다. nullptr은 쿨타임을 소비하지 않는다. 중단해도 바인딩은 유지된다. */
	void SetAttackTargetResolver(FLSResolveAttackTarget ResolverEvent);

	/** 원본 수치를 받는다. 플레이 전에는 준비만 하고 BeginPlay에서 배율을 확정한다.
	 * 재설정으로 진행 중인 쿨타임을 초기화하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool ConfigureAttack(const FLSBasicAttackSettings& NewSettings);
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool ConfigureProjectile(const FLSProjectileAttackSettings& NewSettings);
	FLSProjectileAttackSettings GetProjectileSettings() const
	{
		return ProjectileSettings;
	}
	/** 후보 액터 중심에서 목표까지 발사 경로를 검사한다. 사거리·목표 자격·쿨타임은 판정하지 않는다.
	 * 준비된 근접 공격은 유효한 입력에 true다. 투사체는 설정 반경과 기존 피해 정책으로 몸체·지형을 검사한다.
	 * 상대 Pawn은 이동 계획에서 제외하지만, 오폭이 허용된 같은 진영 Pawn은 경로를 막는다.
	 * 실제 발사체의 몸체 명중·오폭 규칙은 유지되므로 이 결과가 명중을 보장하지 않는다. */
	bool HasClearAttackPathFromLocation(const FVector& SourceLocation, const AActor* Target) const;

	/** 사거리 밖 대상도 지정할 수 있으며 사거리 안에 들어오면 공격한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool StartAttack(AActor* Target);
	/** 명시적으로 참여한 살아 있는 물리 차단물 하나를 공격한다. 오폭을 포함한 공통 피해 정책을 따른다.
	 * 자동 획득과 표적 resolver를 사용하지 않으며 사거리·쿨타임·실제 피해 처리는 기존 공격과 같다. */
	bool StartPhysicalBlockerAttack(AActor* Target);

	/** 현재 대상이 유효하면 유지하고, 대상을 잃었을 때 사거리 안의 적을 탐색한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	bool StartAutoAttack();

	UFUNCTION(BlueprintCallable, Category = "Landship|Attack")
	void StopAttack();

	/** 충돌과 이미 발사한 투사체는 보존하면서 이 컴포넌트의 새 공격을 영구 중단한다. */
	void EndBattleActions();

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	AActor* GetAttackTarget() const
	{
		return AttackTarget.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	float GetCooldownRemaining() const;
	/** 기존 대기시간을 줄이지 않고 다음 공격을 지정한 초만큼 늦춘다. 잘못된 입력은 거부한다. */
	bool DelayNextAttack(float Seconds);
	/** 출처별 공격 간격 배율이다. 1은 해당 출처 제거, 1보다 크면 느려진다. 대기 중 공격 진행률을 보존하며
	 * 경직으로 보류된 시간은 단축하지 않는다. 실행 준비 전의 추가와 종료 후의 추가는 거부한다. */
	bool SetAttackIntervalMultiplier(FName Source, float Multiplier);

	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	FLSBasicAttackSettings GetAttackSettings() const
	{
		return Settings;
	}
	/** 배율 적용 전의 설정은 GetAttackSettings로 조회한다. 준비 전 실제 간격은 0이다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	float GetEffectiveAttackInterval() const
	{
		return EffectiveInterval;
	}
	UFUNCTION(BlueprintPure, Category = "Landship|Attack")
	bool IsConfigured() const
	{
		return bConfigured;
	}

	/** 실제 피해가 적용된 대상을 알린다. 투사체를 가로막은 같은 진영이나 최초 목표 외 적도 포함한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Attack")
	FLSBasicAttackPerformed OnAttackPerformed;
	/** 투사체 발사 알림이다. OnAttackPerformed는 실제 피해가 적용된 명중 시에 발생한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Attack")
	FLSProjectileLaunched OnProjectileLaunched;
	/** 근접 시도는 피해 처리 직전(피해 0 포함), 투사체는 발사 성공 직후 한 번 발생한다.
	 * 소비자는 후속 작업만 큐에 넣고, 처리할 때 약한 참조와 전투 상태를 다시 검증해야 한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Attack")
	FLSAttackExecuted OnAttackExecutedEvent;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
	bool IsValidCombatPair(AActor* Target) const;
	bool IsValidPhysicalBlockerPair(AActor* Target) const;
	void FireProjectile(AActor* Target, const FLSAttackExecution& Execution);
	UFUNCTION()
	void OnProjectileDamage(AActor* Target, float AppliedDamage);
	void UpdateAttack();
	void StartAttackTimer();
	bool ResolveEffectiveInterval(float BaseInterval, float& OutInterval) const;
	AActor* FindNearestTarget() const;

	UPROPERTY(EditDefaultsOnly, Category = "Landship|Attack")
	bool bAutoAttackOnBeginPlay = true;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Attack")
	FLSBasicAttackSettings Settings;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> AttackTarget;

	/** 사거리 진입·사망 검사 주기다. 프레임이 느려져도 밀린 공격을 몰아서 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Attack", meta = (ClampMin = "0.01", Units = "s"))
	float CheckInterval = 0.05f;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Attack")
	FLSProjectileAttackSettings ProjectileSettings;
	FTimerHandle AttackTimer;
	double NextAttackTime = 0.0;
	double AttackBlockedUntil = 0.0;
	TMap<FName, float> IntervalMultipliers;
	bool bConfigured = false;
	bool bHasPendingSettings = false;
	float EffectiveInterval = 0.0f;
	bool bResolvingAttack = false;
	bool bAutoSearching = false;
	bool bPhysicalBlockerAttack = false;
	bool bBattleActionsEnded = false;
	FLSResolveAttackTarget AttackTargetResolverEvent;
	int64 LastExecutionId = 0;
	uint64 StopRevision = 0;
	uint64 CombatLifeSerial = 0;
};
