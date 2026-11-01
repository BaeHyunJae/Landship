#include "Match/LSBattleOutcomePolicy.h"

#include "Engine/World.h"
#include "Match/LSGameState.h"

bool LSBattleOutcome::IsManaged(const UWorld* World)
{
	const ALSGameState* State = World ? World->GetGameState<ALSGameState>() : nullptr;
	return State && State->IsBattleOutcomeEnabled();
}

bool LSBattleOutcome::AreActionsStopped(const UWorld* World)
{
	const ALSGameState* State = World ? World->GetGameState<ALSGameState>() : nullptr;
	return State && State->HasBattleEnded();
}
