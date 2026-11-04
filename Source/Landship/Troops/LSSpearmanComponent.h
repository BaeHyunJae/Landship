#pragma once
#include "Troops/LSTroopCombatComponent.h"
#include "LSSpearmanComponent.generated.h"

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSSpearmanBraceSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float BraceDistance = 600;
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float BraceSetupSeconds = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float FrontalHalfAngle = 60;
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float GuardMultiplier = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float CounterMultiplier = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Brace") float RecoilSeconds = 0.6f;
	bool IsValid() const;
};

/** 창병의 정면 대비와 전투당 한 번의 돌격 방어·반격을 전담한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSSpearmanComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
  public:
	virtual bool IsConfigurationValid() const override;
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents) override;
	virtual FLSChargeDefenseResult ResolveChargeDefense(ALSTroopUnit* Attacker) override;
	/** 대비 시 방향을 고정한다. 유효한 기존 대상이 있으면 자동 회전하지 않는다. */
	bool TryBrace(ALSTroopUnit* Threat);
	void StopBracing();
	bool IsBracing() const
	{
		return BraceThreat.IsValid();
	}
	bool HasSpentGuard() const
	{
		return bGuardSpent;
	}
	AActor* GetBraceThreat() const;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Brace")
	FLSSpearmanBraceSettings Settings;

  protected:
	virtual void CancelSpecialAction() override;
	virtual void TickSpecialAction(float DeltaTime) override;
	virtual bool HasSpecialAction() const override
	{
		return IsBracing();
	}
	virtual FString GetRestingLabel() const override;

  private:
	bool ConsumeGuard(ALSTroopUnit* Attacker);
	TWeakObjectPtr<ALSTroopUnit> BraceThreat;
	double BraceStarted = 0;
	bool bGuardSpent = false;
};
