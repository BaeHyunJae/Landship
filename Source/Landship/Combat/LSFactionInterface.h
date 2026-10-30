#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Combat/LSFaction.h"
#include "LSFactionInterface.generated.h"

/** C++에서 구현하고 Blueprint에서는 조회한다. 대상은 자신의 BeginPlay 준비 시작부터 진영을 고정한다. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class ULSFactionInterface : public UInterface
{
	GENERATED_BODY()
};

class LANDSHIP_API ILSFactionInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Landship|Combat")
	virtual ELSFaction GetFaction() const = 0;
};
