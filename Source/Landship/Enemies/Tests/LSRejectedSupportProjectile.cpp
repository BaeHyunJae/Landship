#include "Enemies/Tests/LSRejectedSupportProjectile.h"

int32 ALSRejectedSupportProjectile::RejectionCount = 0;

void ALSRejectedSupportProjectile::BeginPlay()
{
	Super::BeginPlay();
	++RejectionCount;
	Destroy();
}
