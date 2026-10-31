#include "Castle/Tests/LSWeaponStateObserver.h"

void ALSWeaponStateObserver::Observe(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State)
{
	++Notifications;
	if (Source)
		ReentrantResult = Source->UpgradeWeapon(Weapon);
}
