#pragma once

#include "GameFramework/Actor.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "LSWeaponStateObserver.generated.h"

/** 무장 상태 알림과 알림 중 재진입 차단을 확인하는 테스트 수신자다. */
UCLASS(NotBlueprintable)
class ALSWeaponStateObserver : public AActor
{
	GENERATED_BODY()
  public:
	UPROPERTY()
	TObjectPtr<ULSCastleWeaponComponent> Source;
	int32 Notifications = 0;
	ELSWeaponChangeResult ReentrantResult = ELSWeaponChangeResult::Success;
	UFUNCTION()
	void Observe(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State);
};
