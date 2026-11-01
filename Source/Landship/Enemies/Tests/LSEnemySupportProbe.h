#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSEnemySupportProbe.generated.h"

class ALSEnemyTroop;
class ALSEnemyOutpost;
class ALSBasicAttackTestActor;

/** 전용 실행 인자가 있을 때 실제 NavMesh에서 지원 선택과 유지 결과를 관찰한다. */
UCLASS()
class LANDSHIP_API ALSEnemySupportProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSEnemySupportProbe();
protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void Report(AActor* Requester, FVector Position);
	TWeakObjectPtr<ALSEnemyTroop> Recipient;
	TWeakObjectPtr<ALSEnemyOutpost> OwnOutpost;
	TWeakObjectPtr<ALSEnemyTroop> NearRequester;
	TWeakObjectPtr<ALSEnemyTroop> FarRequester;
	TWeakObjectPtr<ALSBasicAttackTestActor> Opponent;
	FVector Anchor = FVector::ZeroVector;
	FVector UpdateMoveStart = FVector::ZeroVector;
	FVector UpdatedReport = FVector::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	float StageTime = 0;
	float TotalTime = 0;
	int32 Stage = 0;
	int32 Failures = 0;
};
