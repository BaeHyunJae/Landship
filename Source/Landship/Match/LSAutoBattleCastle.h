#pragma once

#include "Castle/LSMobileCastle.h"
#include "LSAutoBattleCastle.generated.h"

/** TABS 편성용 성. 이동 검사 클래스의 몸체를 재사용하고 자동 전투 무장만 소유한다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleCastle : public ALSMobileCastle
{
	GENERATED_BODY()
public:
	ALSAutoBattleCastle();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Weapons")
	TObjectPtr<class ULSCastleWeaponComponent> Weapons;
};
