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
	/** 공격 주체 자신에게 명중한 피해를 허용한다. 다른 아군 오폭과는 별개다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowSelfDamage : 1 = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
