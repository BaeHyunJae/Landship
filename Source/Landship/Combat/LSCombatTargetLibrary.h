#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSCombatTargetLibrary.generated.h"

UCLASS()
class LANDSHIP_API ULSCombatTargetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

  public:
	/** 공통 체력 초기화에 성공한 생존 대상만 허용한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Combat")
	static bool IsCombatAlive(const AActor* Target);

	/** 서로 다른 진영이며 양측 체력이 준비된 생존 대상만 허용한다. 사거리·시야는 검사하지 않으며 명중 피해의 필터로
	 * 쓰지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Combat")
	static bool CanAutoAttackTarget(const AActor* Source, const AActor* Target);
	/** 충돌 원기둥 외곽 사이의 거리다. 같은 규칙을 조준·추적·실제 타격에 사용한다. */
	static double GetAttackDistance(const AActor* Source, const AActor* Target);
	static double GetAttackDistanceAtLocation(const AActor* Source, const AActor* Target, const FVector& SourceLocation);
};
