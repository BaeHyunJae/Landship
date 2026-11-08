#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShellExperiment/LSShellChargeStore.h"
#include "ShellExperiment/LSShellExperimentTypes.h"
#include "LSShellReactiveComponent.generated.h"

class ACharacter;
class UPrimitiveComponent;
class ULSEnemyBehaviorComponent;
struct FLSDamageResult;

UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSShellReactiveComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSShellReactiveComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float Mass = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float MovementResistance = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") bool bExplosive = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float DetonationDamage = 250;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float DetonationRadius = 900;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float DetonationVelocity = 1400;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float DetonationImpactThreshold = 600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float RecoverySpeed = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Charge") bool bCanStoreCharge = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Charge") bool bConductiveRelay = false;
	/** 같은 몸체를 다시 지정하면 전하·식별값·접촉을 유지한다. 다른 몸체나 null은 이전 전하와 양쪽 접촉을 지운다. */
	void SetPhysicalBody(UPrimitiveComponent* InBody);
	UPrimitiveComponent* GetPhysicalBody() const { return Body.Get(); }
	bool IsPhysicalBodyPresent() const;
	bool CanReceiveMomentum() const;
	float GetPhysicsMassKg() const;
	FVector GetSampledSurfaceVelocity(FVector Point) const;
	/** kg·cm/s의 충격을 질량중심에 더한다. 체력 0이어도 남은 강체를 처리한다. */
	bool ApplyMomentum(FVector Impulse);
	void QueueArenaGather(AActor* Source, FVector TargetVelocity, FVector AssemblyVelocity, float Weight, float ResponseSeconds, float MaxAcceleration, float MaxSpeed, float Step);
	void CancelArenaGather(AActor* Source);
	void MarkPropelledByThrust();
	bool IsThrustPropelled() const { return bThrustPropelled; }
	void RestoreThrustPropelled(bool bValue) { bThrustPropelled = bValue; PropelledStableSeconds = 0; }
	uint32 GetBodyGeneration() const { return BodyGeneration; }
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") uint64 BodyId = 0;
	/** 속도 변화량(cm/s)을 받아 이동 저항으로 나눈다. 피해를 적용하지 않는다. */
	void ApplyImpulseVelocity(FVector DeltaVelocity);
	/** 가속도(cm/s²)와 게임 시간 간격을 받는다. 중력장마다 호출해 합산한다. */
	void ApplyGravity(FVector Center, float Acceleration, float DeltaSeconds);
	/** 위치·진영·수치를 복사해 예약한다. 호출 안에서 피해나 제거를 실행하지 않는다. */
	UFUNCTION(BlueprintCallable) bool RequestDetonation();
	bool RequestDetonationWithCause(ELSShellDetonationTrigger Trigger, FVector Origin);
	bool CanMoveByGravity() const;
	bool IsDetonationQueued() const { return bDetonationQueued; }
	bool IsForcedMotionActive() const;
	FVector GetPreResponseVelocity() const { return PreResponseVelocity; }
	FVector GetPreResponseAngularVelocity() const { return PreResponseAngularVelocity; }
	FVector GetMomentumAdjustedLinearVelocity() const;
	float GetMass() const { return Mass; }
	float GetMovementResistance() const { return MovementResistance; }
	FLSShellChargeStore& GetChargeStore() { return ChargeStore; }
	const FLSShellChargeStore& GetChargeStore() const { return ChargeStore; }
	UFUNCTION(BlueprintPure, Category = "Shell Experiment|Charge") float GetStoredCharge() const { return ChargeStore.GetStoredCharge(); }
	/** 현재 물리 몸체가 존재하고 저장을 허용할 때 남은 용량만큼 받아 실제 수용량을 반환한다. */
	float AddCharge(const FLSShellChargePacket& Packet, float Capacity);
	void ClearCharge();
	TArray<UPrimitiveComponent*> GetContactBodies() const;
	TArray<UPrimitiveComponent*> GetStructuralContactBodies() const;
	bool IsContactInsulated(UPrimitiveComponent* OtherBody) const;
	uint64 GetContinuingContactEpoch(UPrimitiveComponent* OtherBody) const;
	/** 양쪽 접촉을 구조적으로 유지한다. 새 접촉 반응을 예약하지 않으며 같은 접촉의 식별값을 보존한다. */
	void SetStructuralContact(ULSShellReactiveComponent* OtherReactive, bool bInsulated);
	/** 구조 유지와 절연만 해제한다. 3cm 이내인 접촉은 다음 Hit에도 새 접촉으로 처리하지 않는다. */
	void ReleaseStructuralContact(ULSShellReactiveComponent* OtherReactive);
	/** 자기 구동과 구별할 외력이 실제 적용된 호출 지점에서 알린다. 0은 외력 기록이 없음을 뜻한다. */
	void NotifyExternalForce();
	uint64 GetLastExternalForceFrame() const { return LastExternalForceFrame; }
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	friend class ALSShellExperimentCoordinator;
	struct FArenaGatherCommand
	{
		TWeakObjectPtr<AActor> Source;
		FVector TargetVelocity;
		FVector AssemblyVelocity;
		float Weight, ResponseSeconds, MaxAcceleration, MaxSpeed, Step;
	};
	TArray<FArenaGatherCommand> ArenaGatherCommands;
	TWeakObjectPtr<class ALSShellDetonationCue> StoredChargeCue;
	bool bThrustPropelled = false;
	float PropelledStableSeconds = 0;
	void UpdateArenaState(float Delta);
	void ApplyArenaGather();
	ELSShellDetonationTrigger DamageFeedbackTrigger = ELSShellDetonationTrigger::Damage;
	void SuspendBehavior();
	void ReleaseSeparatedContacts();
	void ClearContactRelations();
	void RemoveContactRelation(UPrimitiveComponent* OtherBody);
	void ClearConsumedMomentum(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	UFUNCTION() void OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	UFUNCTION() void OnLanded(const FHitResult& Hit);
	UFUNCTION() void OnDied(const FLSDamageResult& Damage);
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<UPrimitiveComponent> Body;
	TWeakObjectPtr<ULSEnemyBehaviorComponent> Behavior;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, uint64> Contacts;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> StructuralContacts;
	FLSShellChargeStore ChargeStore;
	FVector PreResponseVelocity = FVector::ZeroVector;
	FVector PreResponseAngularVelocity = FVector::ZeroVector;
	FVector PreResponseCenter = FVector::ZeroVector;
	FVector PendingMomentumVelocity = FVector::ZeroVector;
	FVector ConsumedAfterSampleVelocity = FVector::ZeroVector;
	uint32 BodyGeneration = 0;
	uint64 LastGravityFrame = 0;
	uint64 LastImpulseFrame = 0;
	uint64 LastExternalForceFrame = 0;
	bool bHasGravity = false;
	bool bForcedMotion = false;
	bool bBehaviorWasTicking = false;
	bool bAvoidanceWasEnabled = false;
	bool bDetonationQueued = false;
};
