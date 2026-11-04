#pragma once

#include "AIController.h"
#include "LSCastleAIController.generated.h"

/** 선회·제동 이동을 쓰는 맵의 성 Blueprint에서 지정한다. 이동 요청은 기존 명령 컴포넌트가 소유한다. */
UCLASS()
class LANDSHIP_API ALSCastleAIController : public AAIController
{
	GENERATED_BODY()
  public:
	ALSCastleAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void FindPathForMoveRequest(const FAIMoveRequest& MoveRequest, FPathFindingQuery& Query,
	                                    FNavPathSharedPtr& OutPath) const override;
};
