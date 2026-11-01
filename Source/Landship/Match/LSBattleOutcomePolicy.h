#pragma once

#include "Match/LSBattleOutcomeTypes.h"

class UWorld;

namespace LSBattleOutcome
{
constexpr ELSBattleOutcome Evaluate(bool bCoreCaptured, bool bCastleDead)
{
	return bCoreCaptured ? (bCastleDead ? ELSBattleOutcome::MutualDestruction : ELSBattleOutcome::Victory)
	                     : (bCastleDead ? ELSBattleOutcome::Defeat : ELSBattleOutcome::InProgress);
}

constexpr bool IsVictory(ELSBattleOutcome Outcome)
{
	return Outcome == ELSBattleOutcome::Victory || Outcome == ELSBattleOutcome::MutualDestruction;
}

LANDSHIP_API bool IsManaged(const UWorld* World);
LANDSHIP_API bool AreActionsStopped(const UWorld* World);
}
