#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSObstacleRuntimeProbe.generated.h"
class ALSUnit;
class ALSTroopUnit;
class ALSCastle;
class ALSMine;
class ALSBarricade;
class ALSTurret;
UCLASS()
class LANDSHIP_API ALSObstacleRuntimeProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSObstacleRuntimeProbe();

  protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

  private:
	UFUNCTION() void OnMineDestroyed(AActor* Actor);
	void Check(bool bPassed, const TCHAR* Name);
	void Finish();
	void DiagnoseCorridor();
	void Cleanup();
	void BeginPhase(int32 Next);
	ALSTroopUnit* SpawnTroop(const FVector& Position, bool bCavalry = false);
	ALSCastle* SpawnCastle(const FVector& Position);
	template <class T> T* Spawn(const FVector& Position);
	bool Move(AActor* Unit, const FVector& Destination);
	TArray<TWeakObjectPtr<AActor>> Owned;
	TWeakObjectPtr<ALSTroopUnit> Troop;
	TWeakObjectPtr<ALSCastle> Castle;
	TWeakObjectPtr<ALSMine> Mine;
	TWeakObjectPtr<ALSBarricade> Barrier;
	TWeakObjectPtr<ALSTurret> Turret;
	FVector Goal = FVector::ZeroVector;
	int32 LastMineState = -1;
	int32 Phase = 0;
	int32 Failures = 0;
	float CorridorTravelBudget = 0;
	float NextDiagnosticTime = 0;
	float PhaseTime = 0;
	float TotalTime = 0;
	float MinimumDistance = TNumericLimits<float>::Max();
	float CastleHealth = 0;
	float CastleMaxHealth = 0;
	float TroopHealth = 0;
	float WorkStarted = -1;
	bool bInitialized = false;
	bool bFinished = false;
	bool bStoppedInWorkRing = false;
	bool bCannonDamageChecked = false;
};
