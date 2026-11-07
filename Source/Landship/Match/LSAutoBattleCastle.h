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
	float ForwardSpeed = 250.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float ReverseSpeed = 110.0f;
	/** 출발 가속도(cm/s²). 기본 전진 최고속도까지 약 1초 걸린다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float DriveAcceleration = 250.0f;
	/** 입력을 놓거나 진행 방향을 반대로 바꿀 때의 제동 감속도(cm/s²). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float DriveBrakingDeceleration = 1250.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float StationaryTurnSpeed = 95.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Drive", meta = (ClampMin = "1"))
	float FullSpeedTurnSpeed = 75.0f;
	bool bPlayerDriveEnabled = false;
};
