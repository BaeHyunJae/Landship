#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LSGameMode.generated.h"

/** 명시적 전장의 종료 사실을 프레임 말미에 모아 결과를 한 번 확정한다. */
UCLASS()
class LANDSHIP_API ALSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALSGameMode();
	virtual void InitGameState() override;
	virtual void StartPlay() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void CollectBattleOutcome(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void StopBattleActions();
	FDelegateHandle WorldTickEndHandle;
};
