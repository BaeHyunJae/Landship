#pragma once

#include "CoreMinimal.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "LSShellTransport.generated.h"

class UPhysicsConstraintComponent;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellCargoSlot
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<ALSShellPhysicalProp> CargoClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform LocalTransform = FTransform::Identity;
	/** 0이면 화물 클래스의 실제 질량 설정을 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", Units = "kg")) float MassOverrideKg = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInsulated = false;
	/** 도착 뒤에도 차대가 지지한다. 해당 화물 또는 차대의 공통 사망 때 원래 몸체를 해제한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bKeepRackedOnDelivery = false;
};

UENUM(BlueprintType)
enum class ELSShellTransportDriveState : uint8 { Waiting, Driving, Suspended, Arrived, Wreck, Invalid, Exiting, Parked, Aligning };

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellTransport : public ALSShellPhysicalProp
{
	GENERATED_BODY()
public:
	ALSShellTransport();
	virtual void Tick(float DeltaSeconds) override;
	virtual FString GetConfigurationError() const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UBoxComponent> Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Route") FVector RouteStart = FVector(-800, -1000, 70);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Route") FVector RouteEnd = FVector(700, -1000, 70);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Route") bool bAutoStartRoute = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Cargo") TArray<FLSShellCargoSlot> CargoSlots;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float ForwardSpeed = 150;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float MaximumAcceleration = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float VelocityResponseSeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float LookAheadDistance = 300;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float LateralVelocityGain = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float MaximumLateralSpeed = 75;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float BrakingDistance = 150;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float YawAngleGain = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float YawRateGain = 4;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float MaximumYawAcceleration = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float MechanicalSuspensionDeltaSpeed = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float MaximumDriveTiltDegrees = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float ResumeMaximumSpeed = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float ResumeStableSeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive") float ArrivalDistance = 10;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive", meta = (Units = "deg", ClampMin = ".01")) float DeliveryHeadingToleranceDegrees = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Drive", meta = (Units = "deg/s", ClampMin = ".01")) float DeliveryAngularSpeedToleranceDegrees = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Wreck") float WreckFriction = .35f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Wreck") float WreckRestitution = .1f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Wreck") float WreckLinearDamping = .05f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Wreck") float WreckAngularDamping = .05f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") ELSShellTransportDriveState DriveState = ELSShellTransportDriveState::Waiting;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float AssemblyMassKg = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FVector AssemblyCenterOfMass = FVector::ZeroVector;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float PeakLatchedGapCm = 0;
	UFUNCTION(BlueprintPure) FString GetDriveStateText() const;
	UFUNCTION(BlueprintPure) bool IsDriving() const { return DriveState == ELSShellTransportDriveState::Driving || DriveState == ELSShellTransportDriveState::Exiting; }
	UFUNCTION(BlueprintPure) bool HasDeliveredCargo() const { return bHasDeliveredCargo; }
	UFUNCTION(BlueprintPure) bool HasRackedCargo() const;
	UFUNCTION(BlueprintPure) TArray<ALSShellPhysicalProp*> GetCargoActors() const;
	TArray<UPrimitiveComponent*> GetAssemblyBodies() const { return GetConnectedPhysicalBodies(); }
	/** 살아 있는 미도착 차량에만 경로를 지정한다. 현재 위치나 속도를 바꾸지 않는다. */
	UFUNCTION(BlueprintCallable) bool StartRoute(FVector Start, FVector End);
	/** 살아 있는 하역 완료 차량만 지정한 월드 경유점을 물리 구동한다. 몸체·속도·화물은 바꾸지 않는다. */
	UFUNCTION(BlueprintCallable) bool StartExitRoute(const TArray<FVector>& Waypoints);
	/** 일정이 정한 실제 첫 퇴장 방향으로 정렬한 뒤 하역한다. 설정하지 않은 차량은 기존 도착 하역을 사용한다. */
	bool SetDeliveryExitDirection(FVector Direction);
	/** 실제 화물의 몸체·속도·체력·전하를 유지하고 결속과 절연만 한 번 해제한다. */
	UFUNCTION(BlueprintCallable) bool ReleaseCargo();
	/** 냉각·접착처럼 매 프레임 힘을 가하지 않는 제어를 사유별로 유지·해제한다. */
	void SetExternalControlHeld(FName Reason, bool bHeld);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void RefreshGeometry() override;
	virtual void PrepareForDeath() override;
	virtual bool RetainsPhysicalBodyOnDeath() const override { return true; }
private:
	bool CreateCargo();
	bool ValidateLoadedGeometry() const;
	bool ReleaseCargoSlot(int32 Index);
	void UnloadDeliveredCargo();
	UFUNCTION() void OnCargoDied(const FLSDamageResult& Damage);
	bool IsGrounded() const;
	void SampleAssembly(FVector& OutVelocity, float& OutYawInertia);
	TArray<UPrimitiveComponent*> GetConnectedPhysicalBodies() const;
	uint64 GetAssemblyExternalForceFrame() const;
	UPROPERTY() TArray<TObjectPtr<ALSShellPhysicalProp>> CargoActors;
	UPROPERTY() TArray<TObjectPtr<UPhysicsConstraintComponent>> CargoConstraints;
	TArray<bool> CargoRacked;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Decorations;
	FVector PreviousAssemblyVelocity = FVector::ZeroVector;
	uint64 ObservedExternalForceFrame = 0;
	float StableSeconds = 0;
	bool bHasVelocitySample = false;
	bool bCargoReleased = false;
	bool bRouteStarted = false;
	bool bHasDeliveredCargo = false;
	bool bExiting = false;
	bool bAlignBeforeDelivery = false;
	bool bAligningForDelivery = false;
	FVector DeliveryExitDirection = FVector::ForwardVector;
	TArray<FVector> ExitWaypoints;
	int32 ExitWaypointIndex = 0;
	FString CargoConfigurationError;
	TSet<FName> ExternalControlHolds;
};
