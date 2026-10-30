#pragma once

#include "CoreMinimal.h"
#include "LSFaction.generated.h"

UENUM(BlueprintType)
enum class ELSFaction : uint8
{
	Ally,
	Enemy
};

namespace LSFaction
{
	inline bool IsValid(ELSFaction Faction)
	{
		return Faction == ELSFaction::Ally || Faction == ELSFaction::Enemy;
	}
}
