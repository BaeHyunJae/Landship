#pragma once

#include "GameFramework/Actor.h"
#include "LSAutoBattleProbe.generated.h"

class ALSAutoBattleGameMode;
class ALSUnit;

/** 전용 개인 맵에서 명시한 실행 플래그가 있을 때만 편성·전투·재시작을 검사한다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSAutoBattleProbe();
	virtual void Tick(float DeltaSeconds) override;

  protected:
	virtual void BeginPlay() override;

  private:
	void Check(bool bCondition, const TCHAR* Label);
	void Finish();
	void TickMatchup(float DeltaSeconds);
	void TickScenarioEditor(float DeltaSeconds);
	void TickExpandedBattlefield(float DeltaSeconds);
	void TickPlacementBrush(float DeltaSeconds);
	void TickDenseRetry(float DeltaSeconds);
	int32 MatchupRound = 0;
	bool bMatchupRunning = false;
	bool bMatchupWaiting = false;
	TWeakObjectPtr<ALSUnit> MatchupAlly;
	TWeakObjectPtr<ALSUnit> MatchupEnemy;
	bool HasFullHealth(AActor* Actor) const;
	TWeakObjectPtr<ALSAutoBattleGameMode> Mode;
	TWeakObjectPtr<ALSUnit> FirstUnit;
	TArray<int32> Ids;
	int32 Stage = 0;
	int32 Failures = 0;
	int32 BudgetAfterPlacement = 0;
	int32 InitialBattleId = 0;
	float Elapsed = 0;
	float StageTime = 0;
};
