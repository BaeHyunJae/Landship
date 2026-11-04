#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"

ALSAutoBattleCastle::ALSAutoBattleCastle()
{
	Weapons = CreateDefaultSubobject<ULSCastleWeaponComponent>(TEXT("Weapons"));
}
