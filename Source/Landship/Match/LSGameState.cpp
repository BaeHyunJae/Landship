#include "LSGameState.h"

bool ALSGameState::SetPopulation(int32 NewCurrentPopulation, int32 NewPopulationLimit)
{
	if (NewCurrentPopulation < 0 || NewPopulationLimit < 0 || NewCurrentPopulation > NewPopulationLimit)
	{
		return false;
	}

	if (CurrentPopulation == NewCurrentPopulation && PopulationLimit == NewPopulationLimit)
	{
		return true;
	}

	CurrentPopulation = NewCurrentPopulation;
	PopulationLimit = NewPopulationLimit;
	OnPopulationChanged.Broadcast(CurrentPopulation, PopulationLimit);
	return true;
}

bool ALSGameState::SetCurrentPopulation(int32 NewCurrentPopulation)
{
	return SetPopulation(NewCurrentPopulation, PopulationLimit);
}

bool ALSGameState::SetPopulationLimit(int32 NewPopulationLimit)
{
	return SetPopulation(CurrentPopulation, NewPopulationLimit);
}

bool ALSGameState::ChangePopulation(int32 Delta)
{
	const int64 NewPopulation = static_cast<int64>(CurrentPopulation) + Delta;
	if (NewPopulation < 0 || NewPopulation > PopulationLimit)
	{
		return false;
	}
	return SetCurrentPopulation(static_cast<int32>(NewPopulation));
}
