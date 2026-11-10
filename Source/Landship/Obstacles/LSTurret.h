#pragma once
#include "Obstacles/LSPathObstacle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSTurret.generated.h"
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class LANDSHIP_API ALSTurret : public ALSPathObstacle
{
	GENERATED_BODY()
  public:
	ALSTurret();
	virtual FString GetConfigurationError() const override;
	void StopObstacleActions();
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FLSTurretCoverTestAccess;
#endif

  protected:
	virtual void PreRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void PrepareForDeath() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void UpdateThreatVisual();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Barrel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ThreatMaterial;
	AActor* ResolveTarget();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSBasicAttackComponent> BasicAttack;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret") FLSBasicAttackSettings AttackSettings;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret") FLSProjectileAttackSettings ProjectileSettings;
	bool bActionsStopped = false;
};
