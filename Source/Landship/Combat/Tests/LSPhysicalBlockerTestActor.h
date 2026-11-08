#pragma once

#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/LSPhysicalBlocker.h"
#include "LSPhysicalBlockerTestActor.generated.h"

UCLASS(NotPlaceable, Transient)
class ALSPhysicalBlockerTestActor : public ALSBasicAttackTestActor, public ILSPhysicalBlocker
{
	GENERATED_BODY()
public:
	bool bPhysicalBlockingEnabled = true;
	virtual UPrimitiveComponent* GetPhysicalBlockingBody() const override;
};
