#pragma once

#include "CoreMinimal.h"
#include "Enemies/LSOutpost.h"
#include "LSEnemyOutpost.generated.h"

/** 기존 거점의 전투 수치를 사용하고 적 전용 판단을 명시적으로 켠다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSEnemyOutpost : public ALSOutpost
{
	GENERATED_BODY()
public:
	ALSEnemyOutpost();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Enemy")
	TObjectPtr<class ULSEnemyBehaviorComponent> Behavior;
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(VisibleAnywhere, Category = "Landship|Enemy")
	TObjectPtr<class UStaticMeshComponent> Body;
};
