#pragma once

#include "Castle/LSCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthTypes.h"
#include "Match/LSBattleOutcomeTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSBattleOutcomeTestObserver.generated.h"

class APlayerController;

/** 렌더링 PIE에서 실제 컨트롤러의 수동 무장 경로를 구동한다. */
UCLASS()
class ULSBattleOutcomeTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
	static bool TryCastleWeapon(APlayerController* Controller, const FVector& Aim);
};

UCLASS(NotPlaceable, Transient)
class ALSBattleOutcomeTestCastle : public ALSCastle
{
	GENERATED_BODY()
public:
	ALSBattleOutcomeTestCastle();
	bool bDestroyDuringDeath = false;
	bool bLeaveHealthUnready = false;
protected:
	virtual void BeginPlay() override;
};

UCLASS(Transient)
class ULSBattleOutcomeTestObserver : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION()
	void OnFinalized(ELSBattleOutcome Outcome);
	UFUNCTION()
	void OnExecuted(const FLSAttackExecution& Execution);
	UFUNCTION()
	void OnCastleDied(const FLSDamageResult& Damage);
	TArray<ELSBattleOutcome> Outcomes;
	TArray<TWeakObjectPtr<AActor>> ExecutionSources;
	TArray<uint64> ExecutionFrames;
	int32 PublicCastleDeaths = 0;
	uint64 FinalizedFrame = 0;
	bool bSawStoppedAttackAtFinalization = false;
	TWeakObjectPtr<ULSBasicAttackComponent> AttackToInspect;
};
