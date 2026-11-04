#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LSTroopPairProductionObserver.generated.h"

UCLASS(Transient)
class ULSTroopPairProductionObserver : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION()
	void OnPopulationChanged(int32 Current, int32 Limit) { Populations.Add(Current); }
	TArray<int32> Populations;
};
