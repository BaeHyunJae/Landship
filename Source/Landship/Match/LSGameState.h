#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "LSGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSOnPopulationChanged, int32, CurrentPopulation, int32, PopulationLimit);

/** 병력 생성과 UI가 같은 인구수 상태를 공유하도록 관리한다. */
UCLASS()
class LANDSHIP_API ALSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Landship|Population")
	int32 GetCurrentPopulation() const { return CurrentPopulation; }

	UFUNCTION(BlueprintPure, Category = "Landship|Population")
	int32 GetPopulationLimit() const { return PopulationLimit; }

	/** 두 값을 함께 검증하여 실패 시 기존 상태를 유지한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Population")
	bool SetPopulation(int32 NewCurrentPopulation, int32 NewPopulationLimit);

	UFUNCTION(BlueprintCallable, Category = "Landship|Population")
	bool SetCurrentPopulation(int32 NewCurrentPopulation);

	UFUNCTION(BlueprintCallable, Category = "Landship|Population")
	bool SetPopulationLimit(int32 NewPopulationLimit);

	/** 양수는 증가, 음수는 감소다. 상한 초과나 정수 범위 초과 시 실패한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Population")
	bool ChangePopulation(int32 Delta);

	/** 초기 표시에는 조회 함수를 쓰고, 이후 변화는 이 알림으로 받는다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Population")
	FLSOnPopulationChanged OnPopulationChanged;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Population")
	int32 CurrentPopulation = 0;

	// 실제 상한은 성장/전투 설정 측에서 지정하며 임의의 밸런스 값을 두지 않는다.
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Population")
	int32 PopulationLimit = 0;
};
