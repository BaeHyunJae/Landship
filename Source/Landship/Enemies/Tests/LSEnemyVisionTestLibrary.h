#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "LSEnemyVisionTestLibrary.generated.h"

class APlayerController;

/** 개발 검증에서 공개 선택 경로를 Python으로 구동한다. */
UCLASS()
class ULSEnemyVisionTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
	static bool TrySelect(APlayerController* Controller, AActor* Target);
	UFUNCTION(BlueprintPure, meta = (DevelopmentOnly))
	static int32 PendingSupportCount(AActor* Target);
	UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
	static bool TryAttackOrder(AActor* Source, AActor* Target);
	UFUNCTION(BlueprintCallable, meta = (DevelopmentOnly))
	static bool TryBasicAttack(AActor* Source, AActor* Target);
};

/** 시야가 켜진 통합 공격 검사인지 실제 게임 월드에서 확인한다. */
UCLASS()
class ALSEnemyVisionRuntimeProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSEnemyVisionRuntimeProbe();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void BeginPlay() override;
private:
	void InspectSubmission(AActor* Source, AActor* Target);
};
