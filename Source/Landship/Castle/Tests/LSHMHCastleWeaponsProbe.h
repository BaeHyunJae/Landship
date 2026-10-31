#pragma once

#include "GameFramework/Actor.h"
#include "LSHMHCastleWeaponsProbe.generated.h"

UCLASS(NotBlueprintable)
class ALSHMHCastleWeaponsProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSHMHCastleWeaponsProbe();
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
	void Capture();
	UPROPERTY()
	TObjectPtr<class ALSMobileCastle> Castle;
	UPROPERTY()
	TObjectPtr<class ALSMobileCastle> EnemyCastle;
	UPROPERTY()
	TArray<TObjectPtr<class ALSTroopUnit>> Enemies;
	UPROPERTY()
	TObjectPtr<class ALSTroopUnit> Ally;
	float Elapsed = 0;
	float StageTime = 0;
	float PushStartX = 0;
	int32 Stage = 0;
	int32 Failures = 0;
};
