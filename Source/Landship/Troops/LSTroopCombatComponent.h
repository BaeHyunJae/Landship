#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSTroopCombatComponent.generated.h"

class ALSUnit;
class ALSTroopUnit;

/** 판단 결과만 반환한다. 이동과 기본 공격의 실행은 전투 제어부가 담당한다. */
struct LANDSHIP_API FLSCombatDirective
{
	AActor* Target = nullptr;
	double ApproachRange = 0;
	bool bHoldPosition = false;
	bool bSuppressBasicAttack = false;
};

/** 돌격 피해를 받기 전에 확정하는 방어 결과. 기본값은 방어 없음이다. */
struct LANDSHIP_API FLSChargeDefenseResult
{
	float EffectScale = 1;
	float CounterDamage = 0;
	float RecoilSeconds = 0;
	bool bGuarded = false;
};

/** 병력 하나가 병종에 맞는 파생 컴포넌트 하나를 소유한다. 기본 구현은 공통 경직만 처리한다. */
UCLASS(Blueprintable, ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSTroopCombatComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSTroopCombatComponent();
	virtual bool IsConfigurationValid() const;
	virtual void SetBattleEnabled(bool bEnabled);
	bool IsBattleEnabled() const
	{
		return bBattleEnabled;
	}
	bool IsStaggered() const;
	/** 전투 활성·생존 상태에서만 적용하며 병종 행동과 기본 공격·이동을 중단한다. */
	void ApplyStagger(float Seconds, const FVector& Velocity);
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents);
	virtual FLSChargeDefenseResult ResolveChargeDefense(ALSTroopUnit* Attacker);

  protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void CancelSpecialAction()
	{
	}
	virtual void TickSpecialAction(float DeltaTime)
	{
	}
	virtual bool HasSpecialAction() const
	{
		return false;
	}
	virtual FString GetRestingLabel() const
	{
		return FString();
	}
	void RefreshTick();
	ALSTroopUnit* GetTroop() const;

  private:
	double StaggerUntil = 0;
	bool bBattleEnabled = false;
};
