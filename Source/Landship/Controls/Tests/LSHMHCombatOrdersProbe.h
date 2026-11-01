#pragma once
#include "GameFramework/Actor.h"
#include "LSHMHCombatOrdersProbe.generated.h"

UCLASS(NotBlueprintable)
class ALSHMHCombatOrdersProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSHMHCombatOrdersProbe();
	virtual bool IsEditorOnly() const override
	{
		return true;
	}
	virtual void Tick(float DeltaSeconds) override;

  protected:
	virtual void BeginPlay() override;

  private:
	void Check(bool bOK, const TCHAR* Message);
	void Finish();
	void TickMeleeAvoidanceProbe();
	UPROPERTY() TArray<TObjectPtr<class ALSTroopUnit>> MeleeUnits;
	float NextMeleeLog = 0;
	UFUNCTION() void OnSpear(AActor* Target, float Damage);
	UFUNCTION() void OnArcher(AActor* Target, float Damage);
	UFUNCTION() void OnCavalry(AActor* Target, float Damage);
	UPROPERTY() TArray<TObjectPtr<class ALSTroopUnit>> Army;
	UPROPERTY() TObjectPtr<class ALSTroopUnit> Enemy;
	UPROPERTY() TObjectPtr<class ALSMobileCastle> TargetCastle;
	int32 Hits[3] = {0, 0, 0};
	FVector LastPosition;
	float BaselineHealth = 0;
	int32 Stage = 0;
	int32 Failures = 0;
	float Elapsed = 0;
	float StageTime = 0;
};
