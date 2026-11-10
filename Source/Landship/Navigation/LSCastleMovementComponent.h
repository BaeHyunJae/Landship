#pragma once

#include "GameFramework/FloatingPawnMovement.h"
#include "AI/RVOAvoidanceInterface.h"
#include "LSCastleMovementComponent.generated.h"

/** 실제 성의 수평 이동이다. 선회 이동은 맵에서 명시적으로 켜며 기본 성은 기존 이동을 유지한다. */
UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCastleMovementComponent : public UFloatingPawnMovement, public IRVOAvoidanceInterface
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement")
	bool bTurnBeforeForward = false;
	/** 전진하기 전 성이 선회하는 속도(도/초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "1"))
	float TurnRate = 90;
	/** 전진 가속도(cm/초²). 최대 속도는 성 수치의 이동 속도를 따른다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "1"))
	float ForwardAcceleration = 300;
	/** 경로의 정지 경계와 방향 변경에 적용하는 감속도(cm/초²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "1"))
	float ForwardBrakingDeceleration = 400;
	/** 이동을 막는 같은 진영의 생존 병력을 수평으로 밀어내는 속도(cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "1"))
	float FriendlyPushSpeed = 600.0f;
	/** 밀려난 자동 병력이 즉시 제자리로 복귀하지 않게 하는 시간(초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "0", Units = "s"))
	float FriendlyPushRecovery = 0.25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ReverseSpeedRatio = 0.55f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Castle Movement", meta = (ClampMin = "1"))
	float PlayerBrakingDeceleration = 1250.0f;
	void SetPlayerDriveInput(float Throttle, float Steering);
	bool IsPlayerDriveEnabled() const { return bPlayerDrive; }

	float GetMaxBrakingDeceleration() const;
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	virtual void StopMovementImmediately() override;
	virtual void StopActiveMovement() override;
	virtual void Deactivate() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	virtual void SetRVOAvoidanceUID(int32 UID) override { AvoidanceUID = UID; }
	virtual int32 GetRVOAvoidanceUID() override { return AvoidanceUID; }
	virtual void SetRVOAvoidanceWeight(float Weight) override { AvoidanceWeight = Weight; }
	virtual float GetRVOAvoidanceWeight() override { return AvoidanceWeight; }
	virtual FVector GetRVOAvoidanceOrigin() override { return GetActorFeetLocation(); }
	virtual float GetRVOAvoidanceRadius() override;
	virtual float GetRVOAvoidanceHeight() override;
	virtual float GetRVOAvoidanceConsiderationRadius() override { return GetRVOAvoidanceRadius(); }
	virtual FVector GetVelocityForRVOConsideration() override { return Velocity; }
	virtual void SetAvoidanceGroupMask(int32 Flags) override { AvoidanceGroup = Flags; }
	virtual int32 GetAvoidanceGroupMask() override { return AvoidanceGroup; }
	virtual void SetGroupsToAvoidMask(int32 Flags) override { GroupsToAvoid = Flags; }
	virtual int32 GetGroupsToAvoidMask() override { return GroupsToAvoid; }
	virtual void SetGroupsToIgnoreMask(int32 Flags) override { GroupsToIgnore = Flags; }
	virtual int32 GetGroupsToIgnoreMask() override { return GroupsToIgnore; }

  protected:
	virtual void ApplyControlInputToVelocity(float DeltaTime) override;
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal,
	                                FHitResult& Hit, bool bHandleImpact = false) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
	void CalcForwardVelocity(float DeltaTime);
	void CalcPlayerDriveVelocity(float DeltaTime);
	bool TryPushFriendlyTroop(const FHitResult& Hit, const FVector& Delta);
	void ClearMoveIntent();
	void RemoveAvoidanceObstacle();
	FVector RequestedVelocity = FVector::ZeroVector;
	bool bHasRequestedVelocity = false;
	bool bRequestedMoveWithMaxSpeed = false;
	bool bPlayerDrive = false;
	float PlayerThrottle = 0;
	float PlayerSteering = 0;
	int32 AvoidanceUID = INDEX_NONE;
	float AvoidanceWeight = 1;
	int32 AvoidanceGroup = 1;
	int32 GroupsToAvoid = -1;
	int32 GroupsToIgnore = 0;
};
