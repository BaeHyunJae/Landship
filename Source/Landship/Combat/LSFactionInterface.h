#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Combat/LSFaction.h"
#include "LSFactionInterface.generated.h"

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
