#pragma once

#include "GameFramework/Actor.h"
#include "LSHMHTroopProbe.generated.h"

class ALSTroopUnit;
class ALSMobileCastle;

/** 전용 실행 옵션에서만 병종의 실제 이동·사거리·추적을 검사한다. */
UCLASS(NotBlueprintable)
class ALSHMHTroopProbe : public AActor
{
	GENERATED_BODY()

  public:
	ALSHMHTroopProbe();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsEditorOnly() const override
	{
		return true;
	}

  protected:
	virtual void BeginPlay() override;

  private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void Hold(ALSTroopUnit* Unit);
	float GetHealth(ALSTroopUnit* Unit) const;
	UFUNCTION()
	void OnSpearHit(AActor* Target, float Damage);
	UFUNCTION()
	void OnArcherHit(AActor* Target, float Damage);
	UFUNCTION()
	void OnCavalryHit(AActor* Target, float Damage);
	UPROPERTY()
	TArray<TObjectPtr<ALSTroopUnit>> Allies;
	UPROPERTY()
	TArray<TObjectPtr<ALSTroopUnit>> Enemies;
	UPROPERTY()
	TObjectPtr<ALSMobileCastle> Castle;
	UPROPERTY()
	TObjectPtr<ALSMobileCastle> EnemyCastle;
	float CastleDetour = 0;
	bool bCastleRetargeted = false;
	TArray<FVector> Starts;
	float Elapsed = 0;
	float StageTime = 0;
	float PreviousTargetHealth = 0;
	int32 Stage = 0;
	int32 RepeatMoves = 0;
	int32 Failures = 0;
	int32 SpearHits = 0;
	int32 ArcherHits = 0;
	int32 CavalryHits = 0;
};
