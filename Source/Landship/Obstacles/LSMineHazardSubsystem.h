#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LSMineHazardSubsystem.generated.h"
class ALSMine;
UCLASS()
class LANDSHIP_API ULSMineHazardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
  public:
	const TArray<TWeakObjectPtr<ALSMine>>& GetMines() const { return Mines; }
	uint64 GetRevision() const { return Revision; }
	void RegisterMine(ALSMine* Mine);
	void UnregisterMine(ALSMine* Mine);
	void NotifyMineChanged(ALSMine* Mine);
	virtual void Deinitialize() override;

  private:
	TArray<TWeakObjectPtr<ALSMine>> Mines;
	uint64 Revision = 0;
};
