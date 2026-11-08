#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LSPhysicalBlocker.generated.h"

class UPrimitiveComponent;
class AActor;

/** 체력·진영과 별개로 실제 몸체가 공통 투사체와 사격 경로를 차단할 때만 구현한다. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class ULSPhysicalBlocker : public UInterface
{
	GENERATED_BODY()
};

class LANDSHIP_API ILSPhysicalBlocker
{
	GENERATED_BODY()
public:
	/** 현재 차단 몸체 하나를 반환한다. null은 물리 차단에 참여하지 않음을 뜻한다. */
	virtual UPrimitiveComponent* GetPhysicalBlockingBody() const = 0;
};

namespace LSPhysicalBlocker
{
/** 액터 또는 등록된 컴포넌트의 명시적 참여와 실제 Query 충돌을 함께 검사한다. 체력은 변경하지 않는다. */
LANDSHIP_API bool IsBlockingBody(const UPrimitiveComponent* Body);
/** 액터 또는 등록된 컴포넌트가 제공하는 현재 차단 몸체를 찾는다. 참여하지 않으면 null이다. */
LANDSHIP_API UPrimitiveComponent* FindBlockingBody(const AActor* Actor);
}
