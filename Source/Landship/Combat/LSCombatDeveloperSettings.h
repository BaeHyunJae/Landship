#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LSCombatDeveloperSettings.generated.h"

/** 피해마다 현재 값을 읽는다. Project Settings 변경은 프로젝트 기본 설정에 저장된다. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Landship Combat Development"))
class LANDSHIP_API ULSCombatDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 공격 주체 자신에게 명중한 피해를 허용한다. 다른 같은 진영 대상의 오폭 허용과 별개다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowSelfDamage : 1 = true;

	/** 같은 진영의 다른 대상에게서 받은 오폭을 양 진영 모두 허용한다. 자기 피해는 별도 설정이며 진영 없는 환경 피해에는 적용하지 않는다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowFriendlyFire : 1 = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
