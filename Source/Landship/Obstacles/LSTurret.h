#pragma once
#include "Obstacles/LSPathObstacle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSTurret.generated.h"
UCLASS(Blueprintable)
class LANDSHIP_API ALSTurret : public ALSPathObstacle
{
	GENERATED_BODY()
  public:
	ALSTurret();
	virtual bool CanBeAutomaticallyAcquired() const override { return true; }
	virtual FString GetConfigurationError() const override;
	void StopObstacleActions();

  protected:
	virtual void BeginPlay() override;
	virtual void PrepareForDeath() override;
	AActor* ResolveTarget();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSBasicAttackComponent> BasicAttack;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret") FLSBasicAttackSettings AttackSettings;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turret") FLSProjectileAttackSettings ProjectileSettings;
	bool bActionsStopped = false;
};
