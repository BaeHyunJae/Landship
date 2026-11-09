#pragma once

#include "Troops/LSTroopCombatComponent.h"
#include "LSBomberComponent.generated.h"

/** 살아서 접촉한 적이 있을 때만 한 번 폭발한다. */
UCLASS()
class LANDSHIP_API ULSBomberComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()

public:
	virtual bool IsConfigurationValid() const override;
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Opponents) override;
	virtual void ResetForNewLife() override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Bomber", meta = (ClampMin = "1", Units = "cm"))
	float BlastRadius = 190.0f;

	bool bSpent = false;
};
