#pragma once

#include "CoreMinimal.h"
#include "LSBattleOutcomeTypes.generated.h"

UENUM(BlueprintType)
enum class ELSBattleOutcome : uint8
{
	InProgress,
	Victory,
	Defeat,
	MutualDestruction
};
