#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSShellSiegeController.generated.h"

class ALSShellExperimentEnemy;
class UTextRenderComponent;
struct FLSAttackExecution;

UENUM(BlueprintType)
enum class ELSShellSiegePhase : uint8 { Disabled, Waiting, Moving, Deploying, Loading, Firing, Reloading, Recovering, Dead, Ended, Invalid };

UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSShellSiegeController : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSShellSiegeController();
	/** Heavy의 공통 체력·공격 초기화 뒤 한 번 호출한다. 실패하면 호출자가 적의 행동을 종료해야 한다. */
	UFUNCTION(BlueprintCallable) bool BeginSiege(AActor* Target, FVector DeploymentPoint);
	UFUNCTION(BlueprintCallable) void SetExternalControlHeld(FName Reason, bool bHeld);
	UFUNCTION(BlueprintPure) FString GetPhaseText() const;
	UFUNCTION(BlueprintPure) FString GetWarningText() const;
	UFUNCTION(BlueprintPure) float GetPhaseRemainingSeconds() const;
	/** 배치 완료 뒤의 반응 저항 배율이다. 실제 질량이나 Reactive의 저장 설정을 바꾸지 않는다. */
	float GetResponseResistanceMultiplier() const;
	void EndSiege();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Siege") FVector DeploymentLocation = FVector(-700, -600, 140);
	/** 기준 배치점에 더하는 유한한 후보들이다. 각 후보의 실제 몸체·예약·사거리·이동 경로를 검사한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Siege") TArray<FVector> DeploymentOffsets = {
		FVector::ZeroVector, FVector(0, 350, 0), FVector(-350, 0, 0), FVector(350, 0, 0) };
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FVector ActiveDeploymentLocation = FVector::ZeroVector;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") bool bHasActiveDeployment = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") bool bDeploymentBlocked = false;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float InitialDelaySeconds = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float DeploySeconds = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float InitialLoadSeconds = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float ReloadSeconds = 4;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float DeployedResistanceMultiplier = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float ArrivalDistance = 25;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float MovementRetrySeconds = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float MechanicalInterruptionDeltaSpeed = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float RecoveryMaximumSpeed = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float RecoveryStableSeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float MaximumTiltDegrees = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Siege") float WarningHeight = 200;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") ELSShellSiegePhase Phase = ELSShellSiegePhase::Disabled;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 ShotsFired = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FString LastInterruption;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FString ConfigurationError;
protected:
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	AActor* ResolveFireTarget() const;
	void SetPhase(ELSShellSiegePhase NewPhase);
	void StopControlledMovement(bool bPreserveVelocity);
	void UpdatePresentation();
	bool IsDeploymentPoseUsable(FVector Candidate, bool bRequirePath, FVector& OutPose) const;
	bool SelectDeploymentPose();
	UFUNCTION() void OnAttackExecuted(const FLSAttackExecution& Execution);
	TWeakObjectPtr<ALSShellExperimentEnemy> Heavy;
	TWeakObjectPtr<AActor> SiegeTarget;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> PhaseLabel;
	TSet<FName> ExternalControlHolds;
	FVector PreviousVelocity = FVector::ZeroVector;
	uint64 ObservedExternalForceFrame = 0;
	float PhaseSeconds = 0;
	float StableSeconds = 0;
	float NextMoveAttemptSeconds = 0;
	double EarliestMoveSeconds = 0;
	bool bShotCommitted = false;
};
