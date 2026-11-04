#pragma once

#include "Navigation/PathFollowingComponent.h"
#include "LSCastlePathFollowingComponent.generated.h"

/** 선회 이동을 쓰는 성이 모서리와 도착 경계에서 제동을 마친 뒤 다음 경로를 받게 한다. */
UCLASS()
class LANDSHIP_API ULSCastlePathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()
  public:
	/** 현재 경로의 다음 정지 경계까지 남은 수평 거리다. 기본 경로 추종에는 적용하지 않는다. */
	bool GetDistanceToNextStop(float& OutDistance) const;

  protected:
	virtual bool HandlePathUpdateEvent() override;
	virtual bool HasReachedDestination(const FVector& CurrentLocation) const override;
	virtual bool HasReachedCurrentTarget(const FVector& CurrentLocation) const override;

  private:
	class ULSCastleMovementComponent* GetCastleMovement() const;
	bool NeedsStopAtCurrentTarget() const;
	bool HasReachedStopGeometry(const FVector& Location) const;
};
