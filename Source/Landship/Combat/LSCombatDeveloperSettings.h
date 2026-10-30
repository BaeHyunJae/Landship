#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LSCombatDeveloperSettings.generated.h"

/** 개발 중 전투 기능을 비교 테스트하는 설정이다. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Landship Combat Development"))
class LANDSHIP_API ULSCombatDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 공격 주체 자신에게 명중한 피해를 허용한다. 아군에게 받는 공격 피해 허용과는 별개다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowSelfDamage : 1 = true;

	/** 아군에게 받는 공격 피해를 허용한다(ex. 아군이 적군을 조준하여 쏜 화살에 아군이 맞음). 아군·적 모두 적용하며 자기 피해는 별도 설정이다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowFriendlyFire : 1 = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
