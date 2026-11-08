#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSShellObstacleResponseComponent.generated.h"

class UPrimitiveComponent;
class ULSEnemyBehaviorComponent;

/** 개선 실험의 정체된 돌격 역할 적이 실제 차단물을 공격하거나 여유가 검증된 옆쪽으로 미는 제한된 대응이다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSShellObstacleResponseComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSShellObstacleResponseComponent();
	/** 적의 정상 목적지를 받는다. 시작 위치·진영·공통 적 행동의 경계를 새로 정하지 않는다. */
	void SetStrategicTarget(AActor* InTarget) { StrategicTarget = InTarget; }
	bool IsResponding() const { return ClearingBody.IsValid(); }
	AActor* GetClearingAttackTarget() const;
	/** kg, 마찰 계수, cm/s²를 받아 정지 마찰을 이길 가속도100cm/s²의 힘을 구한다. 잘못된 값은0이다. */
	static float ComputeClearingForce(float MassKg, float Friction, float Gravity, float Acceleration);
	/** 실제 구조 접촉을 따라 현재 몸체를 한 번씩 반환한다. 일반 접촉은 결합 질량에 포함하지 않는다. */
	static TArray<UPrimitiveComponent*> CollectStructuralBodies(UPrimitiveComponent* Body);
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Obstacle", meta = (ClampMin = "0.1", Units = "s"))
	float BlockedSeconds = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Obstacle", meta = (ClampMin = "0.01", ClampMax = "2", Units = "s"))
	float MaximumResponseSeconds = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Obstacle", meta = (ClampMin = "1", Units = "cm"))
	float ProbeDistance = 500;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Obstacle", meta = (ClampMin = "0", Units = "CentimetersPerSecondSquared"))
	float ClearingAcceleration = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Obstacle", meta = (ClampMin = "1", Units = "cm"))
	float ProgressDistance = 20;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") FString LastResponse;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") float LastClearingForce = 0;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") int32 ResponseCount = 0;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") float LastAssemblyMassKg = 0;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") FString LastProbeSource;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") int32 ReplanRequests = 0;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") bool bLastAlternativePathExists = false;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FLSShellObstacleResponseTestAccess;
#endif
	UPrimitiveComponent* FindFirstBlocker(FVector& OutCorridorDirection);
	TArray<FVector> ReadProbeRoute();
	void RequestAlternativeRoute(const FString& Reason);
	bool ReadClearingAssembly(UPrimitiveComponent* Body, float& OutMass, float& OutFriction, FVector& OutCenter) const;
	bool FindLateralClearance(UPrimitiveComponent* Body, FVector CorridorDirection, FVector& OutDestination) const;
	bool ReadGround(UPrimitiveComponent* Body, FVector Location, float& OutFriction, float& OutGroundZ) const;
	bool CanRestoreBehavior() const;
	void FinishResponse(const FString& Reason);
	void RestoreBehavior();
	TWeakObjectPtr<AActor> StrategicTarget;
	TWeakObjectPtr<ULSEnemyBehaviorComponent> Behavior;
	TWeakObjectPtr<UPrimitiveComponent> ClearingBody;
	TWeakObjectPtr<UPrimitiveComponent> LastAttemptedBody;
	FVector LastProgressPosition = FVector::ZeroVector;
	FVector LastAttemptPosition = FVector::ZeroVector;
	FVector ClearingDestination = FVector::ZeroVector;
	FVector ProbeSegmentOrigin = FVector::ZeroVector;
	FVector ObservedMoveGoal = FVector::ZeroVector;
	TArray<FVector> LastProbeRoute;
	TArray<FVector> ClearingProbeRoute;
	FString ClearingProbeSource;
	float NoProgressSeconds = 0;
	float ResponseSeconds = 0;
	float ForceSeconds = 0;
	bool bBehaviorWasTicking = false;
	bool bRestorePending = false;
	bool bHasObservedMoveGoal = false;
	bool bAttemptedAlternative = false;
};
