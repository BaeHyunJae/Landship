#pragma once

#include "NavigationSystem.h"
#include "LSBattlefieldNavigationSystem.generated.h"

/** 이 전장의 성과 병력에 맞는 두 내비게이션 크기를 등록 전에 정한다. */
UCLASS()
class LANDSHIP_API ULSBattlefieldNavigationSystem : public UNavigationSystemV1
{
	GENERATED_BODY()

  public:
	ULSBattlefieldNavigationSystem(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
