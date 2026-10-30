#pragma once

#include "Castle/LSCastle.h"
#include "LSHMHMovementCastle.generated.h"

/** HMH에서만 사용하는 이동 실험용 성. 전투 수치와 외형은 테스트 전용이다. */
UCLASS(NotBlueprintable)
class ALSHMHMovementCastle : public ALSCastle
{
	GENERATED_BODY()
public:
	ALSHMHMovementCastle();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
private:
	void Check(bool bPassed, const TCHAR* Description);
	void Finish();
	FVector Start;
	FVector Goal;
	float Elapsed = 0.0f;
	float StageTime = 0.0f;
	int32 Stage = 0;
	int32 Failures = 0;
	bool bAutomatic = false;
};
