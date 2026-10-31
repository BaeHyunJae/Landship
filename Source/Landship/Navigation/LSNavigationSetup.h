#pragma once

#include "GameFramework/Actor.h"
#include "LSNavigationSetup.generated.h"

/** 맵에서 지정한 이동 에이전트 크기로 동적 내비게이션을 초기화한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSNavigationSetup : public AActor
{
	GENERATED_BODY()

  public:
	ALSNavigationSetup();

  protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Landship|Navigation", meta = (ClampMin = "1", Units = "cm"))
	float AgentRadius = 142.0f;
	UPROPERTY(EditAnywhere, Category = "Landship|Navigation", meta = (ClampMin = "1", Units = "cm"))
	float AgentHeight = 284.0f;
};
