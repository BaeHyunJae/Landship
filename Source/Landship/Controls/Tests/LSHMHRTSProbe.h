#pragma once

#include "GameFramework/Actor.h"
#include "LSHMHRTSProbe.generated.h"

class ALSHMHRTSUnit;

UCLASS(NotBlueprintable)
class ALSHMHRTSProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSHMHRTSProbe();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void SetStage(int32 NewStage);
	void HoldTestEnemy(ALSHMHRTSUnit* Unit);
	UPROPERTY(Transient)
	TObjectPtr<ALSHMHRTSUnit> Ally;
	UPROPERTY(Transient)
	TObjectPtr<ALSHMHRTSUnit> Enemy;
	UPROPERTY(Transient)
	TObjectPtr<ALSHMHRTSUnit> SecondEnemy;
	float Elapsed = 0;
	float StageTime = 0;
	float PreviousHealth = 0;
	int32 Stage = 0;
	int32 Failures = 0;
	FVector Position;
	FVector FinalGoal;
};
