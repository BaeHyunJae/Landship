#pragma once

#include "Troops/LSTroopCombatComponent.h"
#include "LSMortarComponent.generated.h"

struct FLSUnitStatRow;

/** 제자리에 멈춰 장전하고 고정된 착탄 지점에 곡사 포탄을 발사한다. */
UCLASS()
class LANDSHIP_API ULSMortarComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
public:
	virtual bool IsConfigurationValid() const override;
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents) override;
	virtual void ResetForNewLife() override;
	void SetFieldAttackSpeed(float Multiplier);

protected:
	virtual void CancelSpecialAction() override;

private:
	bool Fire(const FVector& Aim, const FLSUnitStatRow& Stats);

	UPROPERTY(EditDefaultsOnly, Category = "Mortar", meta = (ClampMin = "0.01", Units = "s"))
	float SetupSeconds = 0.65f;
	UPROPERTY(EditDefaultsOnly, Category = "Mortar", meta = (ClampMin = "1", Units = "cm"))
	float BlastRadius = 270.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Mortar", meta = (ClampMin = "1", Units = "cm/s"))
	float ShellSpeed = 1100.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Mortar", meta = (ClampMin = "0", Units = "cm"))
	float ArcHeight = 450.0f;
	TWeakObjectPtr<AActor> SetupTarget;
	double SetupCompleteTime = 0.0;
	double NextFireTime = 0.0;
	float FieldAttackSpeed = 1.0f;
};
