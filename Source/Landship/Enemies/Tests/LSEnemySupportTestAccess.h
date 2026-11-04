#pragma once

#include "Enemies/LSEnemyBehaviorComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
// 경로가 없는 테스트 월드에서도 실제 전투 이벤트의 큐 전달 결과를 관찰한다.
struct FLSEnemySupportTestAccess
{
	static int32 Pending(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->Inbox.Num(); }
	static AActor* Opponent(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->Inbox[0].Opponent.Get(); }
	static FVector ReportedLocation(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->Inbox[0].OpponentLocation; }
	static uint64 RequestId(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->Inbox[0].RequestId; }
	static bool HasActive(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->bHasActiveSupport; }
	static AActor* ActiveRequester(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->ActiveSupport.Requester.Get(); }
	static FVector ActiveLocation(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->ActiveSupport.OpponentLocation; }
	static AActor* StrikeTarget(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->ResolveAttackTarget(); }
	static AActor* AcquiredTarget(const ULSEnemyBehaviorComponent* Behavior, float DetectionRange, FVector& OutGoal)
	{
		return Behavior->SelectTarget(DetectionRange, true, OutGoal);
	}
	static bool FindAttackGoal(const ULSEnemyBehaviorComponent* Behavior, const AActor* Opponent, FVector& OutGoal)
	{
		return Behavior->FindAttackPosition(Opponent->GetActorLocation(), OutGoal, nullptr, Opponent);
	}
	static bool MoveToAttackGoal(ULSEnemyBehaviorComponent* Behavior, const AActor* Opponent, const FVector& Goal)
	{
		const FVector Position = Opponent->GetActorLocation();
		return Behavior->Move(Goal, &Position, Opponent);
	}
	static float MoveAcceptance(const ULSEnemyBehaviorComponent* Behavior) { return Behavior->MovementAcceptance; }
	static void WaitForRangeEntry(ULSEnemyBehaviorComponent* Behavior, AActor* Opponent)
	{
		Behavior->Target = Opponent;
		Behavior->LastDecisionTargetLocation = Opponent->GetActorLocation();
		Behavior->NextDecision = 1000;
	}
};
#endif
