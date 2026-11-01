#pragma once
#include "Obstacles/LSPathObstacle.h"
#include "Combat/LSHealthTypes.h"
#include "LSMine.generated.h"
class USphereComponent;
class UMaterialInstanceDynamic;
UENUM(BlueprintType)
enum class ELSMineState : uint8
{
	Armed,
	Triggered,
	Exploding,
	Spent,
	Disarmed
};
UENUM(BlueprintType)
enum class ELSMineTriggerCause : uint8
{
	Contact,
	DirectDamage,
	Chain
};
UCLASS(Blueprintable)
class LANDSHIP_API ALSMine : public ALSPathObstacle
{
	GENERATED_BODY()
  public:
	ALSMine();
	FVector GetTriggerCenter() const;
	float GetTriggerRadius() const;
	float GetExplosionRadius() const { return ExplosionRadius; }
	float GetDisarmDistance() const { return DisarmDistance; }
	float GetDisarmDuration() const { return DisarmDuration; }
	UFUNCTION(BlueprintPure) ELSMineState GetMineState() const { return State; }
	UFUNCTION(BlueprintPure) bool IsArmed() const;
	UFUNCTION(BlueprintPure) bool IsTriggered() const { return State == ELSMineState::Triggered; }
	bool IsHazardActive() const { return IsArmed() || IsTriggered(); }
	virtual bool IsObstacleActive() const override { return IsHazardActive(); }
	virtual FVector GetInteractionLocation() const override { return GetTriggerCenter(); }
	USphereComponent* GetContactSphere() const { return ContactSphere; }
	bool HasDisarmWorker() const;
	bool TryClaimDisarm(AActor* Worker);
	void ReleaseDisarm(AActor* Worker);
	bool TryCompleteDisarm(AActor* Worker);
	void SetDisarmProgress(AActor* Worker, float Progress);
	void SetHovered(bool bHovered);
	bool RequestTrigger(ELSMineTriggerCause Cause);
	virtual FString GetConfigurationError() const override;

  protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PrepareForDeath() override;
	UFUNCTION() void OnDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnContact(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex,
				   bool bFromSweep, const FHitResult& Hit);
	void Explode();
	void FinishMine(ELSMineState FinalState);
	bool HasBlastLine(UPrimitiveComponent* Body) const;
	void NotifyChanged();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USphereComponent> ContactSphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ProgressBackground;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ProgressFill;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MineMaterial;
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "0.01")) float ExplosionRadius = 600;
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "0.01")) float DisarmDistance = 150;
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "0.01")) float DisarmDuration = 6;
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "0.01")) float ChainDelay = .2f;
	UPROPERTY(EditAnywhere, Category = "Mine", meta = (ClampMin = "0", ClampMax = "1"))
	float CastleMaxHealthFraction = .4f;
	UPROPERTY(VisibleInstanceOnly) ELSMineState State = ELSMineState::Armed;
	UPROPERTY(VisibleInstanceOnly) ELSMineTriggerCause TriggerCause = ELSMineTriggerCause::Contact;
	TWeakObjectPtr<AActor> DisarmWorker;
	FTimerHandle ChainTimer;
	bool bIsHovered = false;
	double TriggeredAt = 0;
};
