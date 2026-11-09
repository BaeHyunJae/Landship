#pragma once

#include "Troops/LSTroopCombatComponent.h"
#include "LSShieldbearerComponent.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** 전투 중 정면 화살에만 적용되는 물리 방패를 소유한다. */
UCLASS()
class LANDSHIP_API ULSShieldbearerComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
public:
	virtual void SetBattleEnabled(bool bEnabled) override;
	bool IsShieldSurface(const UPrimitiveComponent* Component) const;
	bool BlocksArrow(const FVector& FlightDirection) const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> Shield;
};
