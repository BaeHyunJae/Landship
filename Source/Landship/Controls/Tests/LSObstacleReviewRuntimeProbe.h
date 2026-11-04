#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSObstacleReviewRuntimeProbe.generated.h"
class ALSTroopUnit;
class ALSCastle;
class ALSMine;
class ALSBarricade;
UCLASS()
class LANDSHIP_API ALSObstacleReviewRuntimeProbe : public AActor
{
 GENERATED_BODY()
public:
 ALSObstacleReviewRuntimeProbe();
protected:
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
private:
 ALSTroopUnit* SpawnTroop(const FVector& Position, bool bEnemy);
 template<class T> T* Spawn(const FVector& Position);
 UFUNCTION() void OnMineDestroyed(AActor* Actor);
 void MakeRing();
 void Check(bool bPass, const TCHAR* Name);
 void Next(int32 NewPhase);
 void Cleanup();
 void Finish();
 TArray<TWeakObjectPtr<AActor>> Owned;
 TArray<TWeakObjectPtr<ALSMine>> Mines;
 TWeakObjectPtr<ALSTroopUnit> Source;
 TWeakObjectPtr<ALSTroopUnit> Enemy;
 TWeakObjectPtr<ALSTroopUnit> Threat;
 TWeakObjectPtr<ALSCastle> Castle;
 TWeakObjectPtr<ALSBarricade> Barrier;
 TWeakObjectPtr<ALSMine> WorkMine;
 FVector OriginalDestination = FVector::ZeroVector;
 FVector SourceStart = FVector::ZeroVector;
 int32 LastMineState = -1;
 int32 Phase = 0;
 int32 Failures = 0;
 float PhaseTime = 0;
 float TotalTime = 0;
 float EventTime = -1;
 float ThreatInitialHP = 0;
 float NextDiagnostic = 0;
 bool bInitialized = false;
 bool bIssued = false;
 bool bFinished = false;
 bool bObservedBlocked = false;
 bool bObservedAvoidance = false;
};

