#pragma once

#include "Castle/LSMobileCastle.h"
#include "LSAutoBattleCastle.generated.h"

/** TABS 전투에서 아군은 궤도 주행을, 적은 자동 주행을 사용한다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleCastle : public ALSMobileCastle
{
	GENERATED_BODY()
public:
	ALSAutoBattleCastle();
	void EnablePlayerDrive();
	void Drive(float Throttle, float Steering, float DeltaSeconds);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Weapons")
	TObjectPtr<class ULSCastleWeaponComponent> Weapons;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float TurnSpeed = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ReverseSpeedRatio = 0.55f;
	float ForwardSpeed = 0;
};
