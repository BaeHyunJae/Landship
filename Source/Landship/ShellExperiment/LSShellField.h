#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSShellExperimentTypes.h"
#include "LSShellField.generated.h"

UCLASS()
class LANDSHIP_API ALSShellVisualPulse : public AActor
{
	GENERATED_BODY()
public:
	ALSShellVisualPulse();
	void Initialize(float Radius, FLinearColor Color, float Seconds = 0.4f);
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Visual;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Flash;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Material;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FlashMaterial;
	float TargetRadius = 100, Duration = 0.4f, Age = 0;
};

UCLASS()
class LANDSHIP_API ALSShellGravityField : public AActor
{
	GENERATED_BODY()
public:
	ALSShellGravityField();
	void Initialize(class ALSShellExperimentCoordinator* InCoordinator, float Radius, float Seconds, float Acceleration);
	void InitializeSupply(class ALSShellExperimentCoordinator* InCoordinator, const FLSShellShot& Shot);
	bool IsArenaGather() const { return bArenaGather; }
	FLSShellGatherOutput CaptureGatherOutput() const;
	void StopGathering();
	FLSShellGatherBudget ReleaseRemainingGather();
	float GetRemainingSupplyMomentum() const { return SupplyMomentumRemaining; }
	/** 남은 충격 예산만 반환하고 이 장을 종료한다. 이미 적용한 충격은 되돌리지 않는다. */
	float ReleaseRemainingSupplyMomentum();
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void RegisterGravityPrerequisite(class ULSShellReactiveComponent* Reactive);
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Visual;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Material;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Tethers;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> TetherMaterial;
	TWeakObjectPtr<class ALSShellExperimentCoordinator> Coordinator;
	TSet<TWeakObjectPtr<class ULSShellReactiveComponent>> GravityPrerequisites;
	float FieldRadius = 600, Remaining = 2, PullAcceleration = 2600;
	bool bSupplyField = false;
	bool bArenaGather = false;
	FLSShellArenaTuning ArenaTuning;
	float SupplyMomentumRemaining = 0, SupplyMomentumRate = 0, SupplyDeltaSpeedCap = 1500;
	TMap<uint64, float> DeliveredDeltaSpeeds;
	float VisualAge = 0;
};
