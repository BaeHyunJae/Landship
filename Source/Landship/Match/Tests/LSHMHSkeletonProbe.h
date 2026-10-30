#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSHMHSkeletonProbe.generated.h"

class UBoxComponent;

/** HMH 개인 레벨에서만 사용하는 기반 기능 실험 액터다. 전투 구현에 의존하지 않는다. */
UCLASS(NotBlueprintable)
class LANDSHIP_API ALSHMHSkeletonProbe : public AActor
{
	GENERATED_BODY()

public:
	ALSHMHSkeletonProbe();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RunChecks();
	void Check(bool bPassed, const FString& Description);

	UFUNCTION()
	void HandlePopulationChanged(int32 CurrentPopulation, int32 PopulationLimit);

	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UBoxComponent>> Targets;

	TArray<FIntPoint> Notifications;
	int32 CheckCount = 0;
	int32 FailureCount = 0;
};
