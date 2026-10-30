#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSCombatTargetLibrary.generated.h"

UCLASS()
class LANDSHIP_API ULSCombatTargetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 서로 다른 진영이며 양측 체력이 준비된 생존 대상만 허용한다. 사거리·시야는 검사하지 않으며 명중 피해의 필터로 쓰지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Combat")
	static bool CanAutoAttackTarget(const AActor* Source, const AActor* Target);
};
