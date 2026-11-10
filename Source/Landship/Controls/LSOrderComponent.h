#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AITypes.h"
#include "NavigationPath.h"
#include "LSOrderComponent.generated.h"

class ULSBasicAttackComponent;
class ALSMine;
class ALSBarricade;
class ALSUnit;
class ALSTroopUnit;
struct FLSCombatDirective;
struct FLSDamageResult;
struct FPathFollowingResult;

UENUM(BlueprintType)
enum class ELSOrderKind : uint8
{
	Idle,
	Move,
	AttackTarget,
	AttackMove,
	Hold,
	Disarm
};

UENUM(BlueprintType)
enum class ELSOrderFailure : uint8
{
	None,
	Unavailable,
	InvalidTarget,
	Unreachable,
	QueueFull,
	BlockedByBarricade
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSOrder
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite)
	ELSOrderKind Kind = ELSOrderKind::Idle;
	UPROPERTY(BlueprintReadWrite)
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> Target;
};

/** 이동·교전을 한 명령 상태에서 제어해 일반 이동과 공격 이동을 구분한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSOrderComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSOrderComponent();
	/** 명령·경로·콜백을 중지한다. ResumeFromPool 전에는 새 명령을 받지 않는다. */
	void SuspendForPool();
	void ResumeFromPool();
	bool IsUpdatingOrders() const { return bUpdatingOrders; }
	/** 현재 명령과 예약을 비우고 이후 행동 재개를 거절한다. */
	void EndBattleActions();
	UFUNCTION(BlueprintCallable, Category = "Landship|Orders")
	bool SubmitOrder(const FLSOrder& Order, bool bQueue = false);
	/** 현재 명령과 예약은 보존하고, 넉백 착지 뒤 실행을 재개한다. */
	bool ApplyKnockback(const FVector& Velocity);
	/** 낙하와 최소 보류 시간이 끝났으면 넉백 표시만 해제한다. 이동·명령은 실행하지 않는다. */
	bool TryCompleteKnockback();
	bool IsKnockedBack() const
	{
		return bKnockbackActive;
	}
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	ELSOrderKind GetOrderKind() const
	{
		return ActiveOrder.Kind;
	}
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	int32 GetQueuedCount() const
	{
		return PendingOrders.Num();
	}
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	AActor* GetEngagedTarget() const
	{
		return EngagedTarget.Get();
	}
	bool CanAttack() const;
	/** 전장 설정이 자동 병력을 사용하면 아군 병력의 직접 명령을 거부한다. */
	bool IsAutomaticTroop() const;
	/** 성이 밀어낸 자동 병력의 이동만 잠시 보류한다. 교전 표적과 추종 자리는 보존한다. */
	void PauseAfterCastlePush(float Seconds);
	bool PrepareDisarm(ALSMine* Mine, FVector& Destination, double& Length) const;
	bool BeginClaimedDisarm(ALSMine* Mine, const FVector& Destination);
	void CancelDisarm();
	bool CanMove() const;
	bool CanExecuteOrderKind(ELSOrderKind Kind) const;
	FVector GetOrderDestination() const
	{
		return ActiveOrder.Destination;
	}
	/** 생산·양보용 통과 구간을 cm 단위 거리까지 반환한다. 선회 이동 성의 방향 전환·가까운 정지 경계에서는 예상 제동 구간도 포함한다. */
	bool GetPlannedMovementPath(TArray<FVector>& OutPoints, float MaxDistance = MAX_flt) const;
	bool IsYieldingToCastle() const { return bYieldingToCastle; }
	bool DidLastMovementSucceed() const
	{
		return bLastMovementSucceeded;
	}
	ELSOrderFailure GetLastFailure() const
	{
		return LastFailure;
	}
	ALSBarricade* GetBlockingBarricade() const;
	static TArray<FVector> MakeFormation(const FVector& Center, int32 Count, float Spacing);

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "1"))
	int32 MaxQueuedOrders = 64;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0.05"))
	float RepathInterval = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0"))
	float ArrivalTolerance = 30.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0"))
	float AttackApproachPadding = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "1"))
	float TargetRepathDistance = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0.01", Units = "s"))
	float MinAttackRepathInterval = 0.05f;
	/** 성의 현재 위치부터 양보를 미리 판단할 이동 경로 길이(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders|Yield", meta = (ClampMin = "0", Units = "cm"))
	float YieldLookAhead = 1200.0f;
	/** 성과 병력의 충돌 반경에 더할 양보 여유(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders|Yield", meta = (ClampMin = "1", Units = "cm"))
	float YieldClearance = 60.0f;
	/** 같은 진로에서 안전한 양보 위치를 다시 찾는 최소 간격(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders|Yield", meta = (ClampMin = "0.05", Units = "s"))
	float YieldRetryInterval = 0.5f;
	/** 성의 진로와 주변 양보 이동이 안정된 상태에서 허용할 양보 탐색 횟수. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders|Yield", meta = (ClampMin = "1"))
	int32 MaxYieldAttempts = 3;
	/** 추종 재개와 정지 사이의 여유 거리(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders|Escort", meta = (ClampMin = "1", Units = "cm"))
	float CastleFollowSlack = 150.0f;

  private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FLSIdleYieldCrowdedShoulderEscapeTest;
	friend class FLSIdleYieldForwardEscapeTest;
#endif
	friend class FLSIdleYieldContactEscapeTest;
	friend class FLSWallFiringPositionTest;
	friend class FLSCastleBrakingCorridorTest;
	friend struct FLSEnemyFacilityAttackTestAccess;
	void UpdateOrders(float DeltaTime);
	void UpdateAutomaticTroop();
	TArray<ALSUnit*> GatherEngagementTroops(float Radius, bool bAllies) const;
	void ExecuteAutomaticDirective(const FLSCombatDirective& Directive);
	void UpdateIdleYield();
	void CancelIdleYield();
	bool IsIdleYieldPathClear(const TArray<FVector>& BodyCenters) const;
	float GetIdleYieldAlongOffset(const FVector& Closest, const FVector& Direction, const FVector& Side, float CorridorRadius, bool& OutNearbyYielding) const;
	bool IsWaitingFriendlyBody(const AActor* Candidate) const;
	void RequestOrderUpdate();
	void SetEngagementTarget(AActor* Target);
	UFUNCTION()
	void OnEngagedTargetDied(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnOwnerMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	float GetAttackApproachPadding() const;
	UFUNCTION()
	void OnOwnerDamaged(const FLSDamageResult& Damage);
	void TickDisarm(float DeltaTime);
	bool IsDisarmTargetValid(ALSMine* Mine) const;
	bool UsesMineAvoidance() const;
	void SetMineAvoidance(bool bEnabled);
	void StopForMine();
	bool ResolveDestination(const FVector& Goal, FVector& OutDestination, FNavPathSharedPtr* OutPath = nullptr, bool* OutFullPathFailed = nullptr) const;
	ALSBarricade* FindBlockingBarricade(const FVector& Goal) const;
	bool GetNavigationOriginHeight(double& OutHeight) const;
	bool ResolveAttackDestination(AActor* Target, FVector& OutDestination, bool bAvoidMines,
	                              bool* OutMineBlocked = nullptr) const;
	bool PrepareOrder(const FLSOrder& Order, FLSOrder& OutOrder);
	void OnMovementFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
	bool MoveTo(const FVector& Goal, bool bAttackApproach = false, float CustomAcceptance = -1.0f);
	void StopMovement();
	bool IsMoving() const;
	bool IsHostile(AActor* Target) const;
	bool IsInAttackRange(AActor* Target) const;
	float GetDetectionRange() const;
	AActor* FindEnemy(float Range) const;
	void BeginOrder(const FLSOrder& Order);
	void FinishOrder();
	void Engage(AActor* Target, bool bAllowChase);
	UPROPERTY(Transient)
	FLSOrder ActiveOrder;
	UPROPERTY(Transient)
	TArray<FLSOrder> PendingOrders;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> EngagedTarget;
	UPROPERTY(Transient)
	TObjectPtr<ULSBasicAttackComponent> Attack;
	TWeakObjectPtr<ALSMine> DisarmMine;
	float DisarmElapsed = 0;
	bool bDisarmWorking = false;
	bool bMineBlocked = false;
	FVector MineBlockedGoal = FVector::ZeroVector;
	bool bMineBlockedAttackApproach = false;
	double NextRepathTime = 0;
	double LastAttackRepathTime = -1;
	FVector LastAttackTargetLocation = FVector::ZeroVector;
	FTimerHandle OrderUpdateTimer;
	bool bUpdatingOrders = false;
	double CastlePushResumeAfter = 0;
	bool bAttackApproaching = false;
	bool bHasEngagement = false;
	bool bBattleActionsEnded = false;
	FAIRequestID MovementRequestId = FAIRequestID::InvalidRequest;
	bool bMovementCompleted = false;
	bool bLastMovementSucceeded = false;
	bool bKnockbackActive = false;
	bool bYieldingToCastle = false;
	FVector YieldDestination = FVector::ZeroVector;
	FVector YieldCastleOrigin = FVector::ZeroVector;
	FVector YieldCastleGoal = FVector::ZeroVector;
	int32 YieldAttempts = 0;
	bool bYieldSpaceChanging = false;
	double NextYieldTime = 0;
	double KnockbackResumeAfter = 0;
	TWeakObjectPtr<ALSBarricade> BlockingBarricade;
	ELSOrderFailure LastFailure = ELSOrderFailure::None;
	uint64 OrderLifeSerial = 0;
};
