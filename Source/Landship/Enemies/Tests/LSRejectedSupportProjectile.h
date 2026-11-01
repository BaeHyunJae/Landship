#pragma once

#include "Combat/LSBasicProjectile.h"
#include "LSRejectedSupportProjectile.generated.h"

/** 생성 완료 중 제거되는 투사체로 발사 확정 전 실패 경로를 검사한다. */
UCLASS(NotPlaceable, Transient)
class ALSRejectedSupportProjectile : public ALSBasicProjectile
{
	GENERATED_BODY()
public:
	static int32 RejectionCount;
protected:
	virtual void BeginPlay() override;
};
