#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LSCombatDeveloperSettings.generated.h"

/** Project Settings 변경은 프로젝트 기본 설정에 저장된다. 각 설정의 적용 시점은 항목 설명을 따른다. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Landship Combat Development"))
class LANDSHIP_API ULSCombatDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 기본 공격 간격을 이 값으로 나눈다. 유한한 양수만 사용하며 플레이 시작 시 확정한다. 진행 중 변경은 다음 플레이부터 반영하며 성 무장에는 적용하지 않는다. */
	UPROPERTY(Config, EditAnywhere, Category = "Basic Attack Testing",
	          meta = (ClampMin = "0.0", DisplayName = "기본 공격 속도 배율"))
	float BasicAttackSpeedMultiplier = 1.0f;

	/** 공격 주체 자신에게 명중한 피해를 허용한다. 다른 같은 진영 대상의 오폭 허용과 별개다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowSelfDamage : 1 = true;

	/** 양 진영의 같은 편 피해를 허용한다. 일반 직선탄은 금지 시 같은 편을 통과하고 장애물 지정 탄은 막히되 피해를 주지 않는다. 판정 시 현재 값을 쓰며 자기 피해·환경 위험은 별도 정책이다. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat Testing")
	uint8 bAllowFriendlyFire : 1 = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
