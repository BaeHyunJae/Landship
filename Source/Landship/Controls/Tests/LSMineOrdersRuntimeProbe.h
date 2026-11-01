#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSMineOrdersRuntimeProbe.generated.h"

class ALSTroopUnit;
class ALSMine;

UCLASS()
class LANDSHIP_API ALSMineOrdersRuntimeProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSMineOrdersRuntimeProbe();

  protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

  private:
	UFUNCTION() void OnMineDestroyed(AActor* Actor);
	ALSTroopUnit* SpawnTroop(const FVector& Position, bool bEnemy);
	ALSMine* SpawnMine(const FVector& Position);
	void BeginPhase(int32 Next);
	void Cleanup();
	void Check(bool bPass, const TCHAR* Name);
	void Finish();
	void Sample();
	void Diagnose();
	TArray<TWeakObjectPtr<AActor>> Owned;
	TArray<TWeakObjectPtr<ALSMine>> Mines;
	TWeakObjectPtr<ALSTroopUnit> Source;
	TWeakObjectPtr<ALSTroopUnit> Enemy;
	TWeakObjectPtr<ALSMine> ForwardMine;
	FVector Destination = FVector::ZeroVector;
	int32 Phase = 0;
	int32 Failures = 0;
	int32 LastMineState = -1;
	float PhaseTime = 0;
	float TotalTime = 0;
	float NextDiagnostic = 0;
	float EnemyInitialHealth = 0;
	float StoppedSince = -1;
	float ReleasedAt = -1;
	float MinimumDistance = TNumericLimits<float>::Max();
	float MaximumBlockedSpeed = 0;
	uint64 RevisionBefore = 0;
	bool bInitialized = false;
	bool bIssued = false;
	bool bSawAvoidance = false;
	bool bFinished = false;
	bool bRetentionFailed = false;
};
