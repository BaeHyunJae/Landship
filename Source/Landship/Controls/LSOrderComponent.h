#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AITypes.h"
#include "LSOrderComponent.generated.h"

class ULSBasicAttackComponent;
class ALSMine;
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
	QueueFull
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
	bool PrepareDisarm(ALSMine* Mine, FVector& Destination, double& Length) const;
	bool BeginClaimedDisarm(ALSMine* Mine, const FVector& Destination);
	void CancelDisarm();
	bool CanMove() const;
	bool CanExecuteOrderKind(ELSOrderKind Kind) const;
	FVector GetOrderDestination() const
	{
		return ActiveOrder.Destination;
	}
	bool DidLastMovementSucceed() const
	{
		return bLastMovementSucceeded;
	}
	ELSOrderFailure GetLastFailure() const
	{
		return LastFailure;
	}
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

  private:
	void UpdateOrders(float DeltaTime);
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
	bool ResolveDestination(const FVector& Goal, FVector& OutDestination) const;
	bool GetNavigationOriginHeight(double& OutHeight) const;
	bool ResolveAttackDestination(AActor* Target, FVector& OutDestination, bool bAvoidMines,
	                              bool* OutMineBlocked = nullptr) const;
	bool PrepareOrder(const FLSOrder& Order, FLSOrder& OutOrder) const;
	void OnMovementFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
	bool MoveTo(const FVector& Goal, bool bAttackApproach = false);
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
	bool bAttackApproaching = false;
	bool bHasEngagement = false;
	bool bBattleActionsEnded = false;
	FAIRequestID MovementRequestId = FAIRequestID::InvalidRequest;
	bool bMovementCompleted = false;
	bool bLastMovementSucceeded = false;
	bool bKnockbackActive = false;
	double KnockbackResumeAfter = 0;
	ELSOrderFailure LastFailure = ELSOrderFailure::None;
	uint64 OrderLifeSerial = 0;
};
