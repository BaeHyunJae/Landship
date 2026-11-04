#include "Castle/LSCastleAIController.h"
#include "Navigation/LSCastlePathGeometry.h"
#include "Navigation/LSCastlePathFollowingComponent.h"

ALSCastleAIController::ALSCastleAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<ULSCastlePathFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ALSCastleAIController::FindPathForMoveRequest(const FAIMoveRequest& MoveRequest, FPathFindingQuery& Query,
                                                 FNavPathSharedPtr& OutPath) const
{
	Super::FindPathForMoveRequest(MoveRequest, Query, OutPath);
	LSCastlePathGeometry::OffsetCorners(GetPawn(), OutPath);
}
