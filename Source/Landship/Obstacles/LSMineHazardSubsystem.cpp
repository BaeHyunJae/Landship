#include "Obstacles/LSMineHazardSubsystem.h"
#include "Obstacles/LSMine.h"
void ULSMineHazardSubsystem::RegisterMine(ALSMine* Mine)
{
	if (IsValid(Mine) && Mine->GetWorld() == GetWorld() && !Mines.Contains(Mine))
	{
		Mines.Add(Mine);
		++Revision;
	}
}
void ULSMineHazardSubsystem::UnregisterMine(ALSMine* Mine)
{
	if (Mines.Remove(Mine) > 0)
		++Revision;
}
void ULSMineHazardSubsystem::NotifyMineChanged(ALSMine* Mine)
{
	if (Mines.Contains(Mine))
		++Revision;
}
void ULSMineHazardSubsystem::Deinitialize()
{
	Mines.Reset();
	++Revision;
	Super::Deinitialize();
}
