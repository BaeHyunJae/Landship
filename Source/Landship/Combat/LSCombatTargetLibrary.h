#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSCombatTargetLibrary.generated.h"

UCLASS()
class LANDSHIP_API ULSCombatTargetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 자동 표적 선정용이다. 명중 피해에 적용하면 아군 오폭까지 막히므로 구분한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Combat")
	static bool CanAutoAttackTarget(const AActor* Source, const AActor* Target);
};
