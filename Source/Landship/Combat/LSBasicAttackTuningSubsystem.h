#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LSBasicAttackTuningSubsystem.generated.h"

/** 같은 플레이의 기존 대상과 나중에 생성된 대상이 동일한 기본 공격 실험값을 사용하도록 보관한다. */
UCLASS(BlueprintType)
class LANDSHIP_API ULSBasicAttackTuningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 플레이 시작 시 확정한 배율이다. 시작 전·잘못된 설정·서브시스템 종료 시 실패하며 출력값을 바꾸지 않는다. 공격 간격에 적용하는 쪽에서 한 번만 나눈다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Combat|Testing")
	bool TryGetBasicAttackSpeedMultiplier(float& OutMultiplier) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Combat|Testing")
	float BasicAttackSpeedMultiplier = 0.0f;

	bool bHasValidMultiplier = false;
};
