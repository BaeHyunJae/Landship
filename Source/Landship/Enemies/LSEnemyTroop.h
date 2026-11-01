#pragma once

#include "Troops/LSTroopUnit.h"
#include "LSEnemyTroop.generated.h"

/** 공용 병종에 적 행동만 명시적으로 연결한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSEnemyTroop : public ALSTroopUnit
{
	GENERATED_BODY()
public:
	ALSEnemyTroop();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSEnemyBehaviorComponent> Behavior;
};
