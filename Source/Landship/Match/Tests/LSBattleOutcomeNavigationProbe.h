#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "LSBattleOutcomeNavigationProbe.generated.h"

class ALSBattlefieldSetup;
class ALSTroopUnit;
class ALSEnemyTroop;
class ALSEnemyOutpost;
class ALSBasicAttackTestActor;

/** 저작된 EnemyCombat 맵의 PIE 월드에서만 명시적으로 생성하는 이동 종료 검사다. */
UCLASS(Transient, NotPlaceable)
class ALSBattleOutcomeNavigationProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSBattleOutcomeNavigationProbe();
	bool bVictory = true;
	UFUNCTION(BlueprintPure)
	FString GetReport() const { return Report; }
	virtual void Tick(float DeltaSeconds) override;
private:
	void Check(bool Passed, const TCHAR* Message);
	void Finish();
	void SendSupport();
	TWeakObjectPtr<ALSBattlefieldSetup> Setup;
	TWeakObjectPtr<ALSTroopUnit> Friendly;
	TWeakObjectPtr<ALSEnemyTroop> Marcher;
	TWeakObjectPtr<ALSEnemyTroop> Supporter;
	TWeakObjectPtr<ALSEnemyTroop> Retry;
	TWeakObjectPtr<ALSEnemyOutpost> Requester;
	TWeakObjectPtr<ALSBasicAttackTestActor> Opponent;
	TArray<FVector> Starts;
	TArray<FVector> Stopped;
	TArray<uint32> RequestIds;
	FString Report;
	float Elapsed = 0;
	float StageTime = 0;
	float NextDiagnosticTime = 0;
	int32 Stage = 0;
	int32 Failures = 0;
	int32 Samples = 0;
	bool bStable = true;
};

UCLASS()
class ULSBattleOutcomeNavigationTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 독립 PIE 실행마다 새로 호출한다. 월드의 기존 전투 행동은 검사 준비 중 정지한다. */
	UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
	static ALSBattleOutcomeNavigationProbe* StartNavigationProbe(AActor* WorldActor, bool bVictory);
};
