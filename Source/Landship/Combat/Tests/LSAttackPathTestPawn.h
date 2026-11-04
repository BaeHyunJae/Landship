#pragma once

#include "Combat/LSFactionInterface.h"
#include "GameFramework/Pawn.h"
#include "LSAttackPathTestPawn.generated.h"

UCLASS(NotPlaceable, Transient)
class ALSAttackPathTestPawn : public APawn, public ILSFactionInterface
{
	GENERATED_BODY()

public:
	ELSFaction Faction = ELSFaction::Ally;
	virtual ELSFaction GetFaction() const override { return Faction; }
};
