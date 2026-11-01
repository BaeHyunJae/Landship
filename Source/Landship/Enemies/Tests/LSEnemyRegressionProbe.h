#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSEnemyRegressionProbe.generated.h"

class ALSEnemyTroop;
class ALSEnemyOutpost;
class ALSMobileCastle;
class ALSBasicAttackTestActor;
struct FLSDamageResult;

/** 명시적인 실행 플래그가 있을 때만 실제 경로 실패와 복귀 중 전투를 관찰한다. */
UCLASS()
class LANDSHIP_API ALSEnemyRegressionProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSEnemyRegressionProbe();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void Freeze(ALSEnemyTroop* Unit);
	UFUNCTION()
	void OnOpponentDamaged(const FLSDamageResult& Damage);
	TWeakObjectPtr<ALSMobileCastle> Castle;
	TWeakObjectPtr<ALSEnemyOutpost> Core;
	TWeakObjectPtr<ALSEnemyTroop> Unit;
	TWeakObjectPtr<ALSBasicAttackTestActor> Opponent;
	TWeakObjectPtr<AActor> Blocker;
	FString Case;
	FVector Start = FVector::ZeroVector;
	FVector Center = FVector::ZeroVector;
	double RetryDeadline = 0;
	float Elapsed = 0;
	float StageTime = 0;
	float MaximumDelta = 0;
	float MaximumRadius = 0;
	int32 Stage = 0;
	int32 Failures = 0;
	int32 Samples = 0;
	bool bDamagedDuringReturn = false;
	bool bInvalidState = false;
	bool bSupportObserved = false;
	bool bResumedSupportMovement = false;
	bool bObservedBandReentry = false;
};
