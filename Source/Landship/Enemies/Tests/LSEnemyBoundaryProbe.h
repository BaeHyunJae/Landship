#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSHealthTypes.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSEnemyBoundaryProbe.generated.h"

class ALSEnemyTroop;
class ALSEnemyOutpost;
class ALSMobileCastle;
class ALSBasicAttackTestActor;

/** 경계 보정과 밀집 진격은 별도 실행으로 관찰하여 기존 전투 검증과 격리한다. */
UCLASS()
class LANDSHIP_API ALSEnemyBoundaryProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSEnemyBoundaryProbe();
protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void Freeze(ALSEnemyTroop* Unit);
	UFUNCTION()
	void OnCastleDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnExecuted(const FLSAttackExecution& Execution);
	TWeakObjectPtr<ALSMobileCastle> Castle;
	TWeakObjectPtr<ALSEnemyOutpost> Core;
	TWeakObjectPtr<ALSEnemyTroop> Guard;
	TWeakObjectPtr<ALSBasicAttackTestActor> Opponent;
	TArray<TWeakObjectPtr<ALSEnemyTroop>> Marchers;
	TMap<TWeakObjectPtr<ALSEnemyTroop>, FVector> Starts;
	TSet<TWeakObjectPtr<AActor>> DamageSources;
	TSet<TWeakObjectPtr<AActor>> ExecutionSources;
	FVector ApproachStart = FVector::ZeroVector;
	float OpponentHealth = 0;
	float MaximumRadius = 0;
	float MaximumDelta = 0;
	float FirstBoundaryDamageTime = -1;
	int32 SoftBoundaryEntries = 0;
	bool bWasReentering = false;
	float StageTime = 0;
	float TotalTime = 0;
	int32 Stage = 0;
	int32 Failures = 0;
	bool bDense = false;
	bool bMovingCastle = false;
	bool bUnexpectedHomeReturn = false;
	bool bStrategicSupportDiversion = false;
};
