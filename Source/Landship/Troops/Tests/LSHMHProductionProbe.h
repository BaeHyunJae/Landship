#pragma once

#include "GameFramework/Actor.h"
#include "LSHMHProductionProbe.generated.h"

UCLASS(NotBlueprintable)
class ALSHMHProductionProbe : public AActor
{
	GENERATED_BODY()
  public:
	ALSHMHProductionProbe();
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
	UPROPERTY()
	TObjectPtr<class ALSMobileCastle> Castle;
	UPROPERTY()
	TObjectPtr<class UBoxComponent> Blocker;
	float Elapsed = 0;
	float StageTime = 0;
	int32 Stage = 0;
	int32 Failures = 0;
};
