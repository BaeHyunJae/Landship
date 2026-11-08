#include "Combat/Tests/LSPhysicalBlockerTestActor.h"

#include "Components/PrimitiveComponent.h"

UPrimitiveComponent* ALSPhysicalBlockerTestActor::GetPhysicalBlockingBody() const
{
	return bPhysicalBlockingEnabled ? Cast<UPrimitiveComponent>(GetRootComponent()) : nullptr;
}
