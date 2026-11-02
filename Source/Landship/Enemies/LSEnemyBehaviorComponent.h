#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AITypes.h"
#include "LSEnemyBehaviorComponent.generated.h"

class ALSOutpost;
class ULSBasicAttackComponent;
class ULSHealthComponent;
class AAIController;
struct FPathFollowingResult;
struct FLSDamageResult;
struct FLSAttackExecution;

UENUM(BlueprintType)
enum class ELSEnemyRole : uint8 { Guard, Assault };
UENUM(BlueprintType)
enum class ELSEnemyState : uint8
{
	WaitingForSetup, WaitingForPath, GuardIdle, Marching, MarchRecovery, Engaging, Supporting, Returning, ReturnBlocked, ReenteringDomain, Ended, Dead
};

/** 적 전용 저작 클래스에서만 추가한다. 명령 큐와 이동 제어권을 공유하지 않는다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSEnemyBehaviorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSEnemyBehaviorComponent();
	/** 이동·표적·지원 상태를 비우고 이후 행동 재개를 거절한다. */
	void EndBattleActions();
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Landship|Enemy")
	ELSEnemyRole Role = ELSEnemyRole::Guard;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Landship|Enemy")
	TObjectPtr<ALSOutpost> GuardedOutpost;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0.01"))
	float DecisionInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0"))
	float SupportAcceptanceRadius = 1200.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0"))
	float ChaseRadius = 1000.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "1"))
	float RecoveryAdvanceDistance = 300.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0"))
	float BoundaryAllowance = 40.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "1"))
	float ReturnAreaRadius = 100.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0.01"))
	float PendingSupportLifetime = 3.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "1"))
	float ArrivalTolerance = 20.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "1"))
	float RepathDistance = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0.01"))
	float FailedPathRetryInterval = 1.0f;
	/** 이동 중 실제 위치 진행이 없는 시간을 실패로 판정한다. 전체 이동 시간 제한이 아니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Enemy", meta = (ClampMin = "0.1"))
	float MovementTimeout = 12.0f;
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	ELSEnemyState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	AActor* GetTarget() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	FVector GetInitialPosition() const { return InitialPosition; }
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	FVector GetEngagementCenter() const { return EngagementCenter; }
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	float GetRecoveryProgress() const { return RecoveryProgress; }
	UFUNCTION(BlueprintPure, Category = "Landship|Enemy")
	FString GetLastStopReason() const { return LastStopReason; }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FLSEnemySupportTestAccess;
	friend struct FLSEnemyRegressionTestAccess;
#endif
	struct FLSEnemySupportRequest
	{
		TWeakObjectPtr<AActor> Requester;
		TWeakObjectPtr<AActor> Opponent;
		FVector RequesterLocation = FVector::ZeroVector;
		FVector OpponentLocation = FVector::ZeroVector;
		double Time = 0;
		uint64 RequestId = 0;
	};
	void Decide();
	AActor* ResolveAttackTarget() const;
	AActor* SelectTarget(float Radius, bool bRequirePath, FVector& OutGoal, const AActor* RequiredOpponent = nullptr) const;
	bool TryEngage(AActor* Candidate, FVector Goal, const AActor* StrategicCastle);
	FString GetConfigurationError(bool bCheckPlacement, const FVector& Placement) const;
	bool IsSameFaction(const AActor* Other) const;
	bool CanAttack() const;
	bool ShouldEndBattleActions() const;
	bool IsStationary() const;
	bool GetMovementDomain(FVector& Center, float& Radius, const FVector* ProvisionalChaseCenter = nullptr) const;
	bool ResolvePath(const FVector& Desired, FVector& OutGoal, bool bConstrain = true, const FVector* ProvisionalChaseCenter = nullptr) const;
	bool FindAttackPosition(const FVector& OpponentLocation, FVector& OutGoal, const FVector* ProvisionalChaseCenter = nullptr) const;
	bool GetNavigationOriginHeight(double& OutHeight) const;
	bool Move(const FVector& Goal, const FVector* AttackPosition = nullptr);
	uint32 GetRouteConditions(const FVector& Goal) const;
	void RecordMovementFailure();
	void StopMovement();
	void OnMovementFinished(FAIRequestID Id, const FPathFollowingResult& Result);
	void BeginReturn(const FString& Reason);
	void ReturnHome();
	void ReenterDomain();
	void BeginRecovery();
	void March();
	void EmitSupport(AActor* Opponent, const FVector& OwnLocation, const FVector& OpponentLocation);
	void ReceiveSupport(const FLSEnemySupportRequest& Request);
	bool GetSupportGoal(const FLSEnemySupportRequest& Request, FVector& OutGoal) const;
	bool SelectSupport();
	UFUNCTION()
	void OnAttackExecuted(const FLSAttackExecution& Execution);
	UFUNCTION()
	void OnDamaged(const FLSDamageResult& Damage);
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Enemy")
	ELSEnemyState State = ELSEnemyState::WaitingForSetup;
	ELSEnemyState StateBeforeReentry = ELSEnemyState::GuardIdle;
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Enemy")
	FString LastStopReason;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<ULSBasicAttackComponent> Attack;
	TWeakObjectPtr<ULSHealthComponent> Health;
	TWeakObjectPtr<AAIController> BoundController;
	FVector InitialPosition = FVector::ZeroVector;
	FVector EngagementCenter = FVector::ZeroVector;
	FVector MovementGoal = FVector::ZeroVector;
	float MovementAcceptance = 0;
	FVector LastMovementProgressPosition = FVector::ZeroVector;
	TArray<FVector> RecoveryRoute;
	FVector RecoveryGoal = FVector::ZeroVector;
	float RecoveryProgressBase = 0;
	float RecoveryProgress = 0;
	float DetectionRadius = 0;
	double NextDecision = 0;
	FVector LastDecisionTargetLocation = FVector::ZeroVector;
	double MovementStart = 0;
	uint32 MovementConditions = 0;
	FIntVector MovementConditionKey = FIntVector::ZeroValue;
	TMap<FIntVector, uint32> FailedReturnConditions;
	double NextMoveAttempt = 0;
	FAIRequestID MovementRequestId = FAIRequestID::InvalidRequest;
	bool bMovementFailed = false;
	bool bMovementFailedBeforeReentry = false;
	bool bConfigurationValid = true;
	bool bBattleActionsEnded = false;
	bool bConfigurationWarningSent = false;
	bool bEngagementLatched = false;
	bool bRecovering = false;
	bool bHasActiveSupport = false;
	FLSEnemySupportRequest ActiveSupport;
	TArray<FLSEnemySupportRequest> Inbox;
	uint64 LastRequestId = 0;
};
