#pragma once

#include "GameFramework/Actor.h"
#include "Combat/LSHealthTypes.h"
#include "LSBasicProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSProjectileDamageApplied, AActor*, Target, float, AppliedDamage);

/** 발사 시점의 피해·진영을 보관하는 직선 비유도 투사체다. 피해 정책은 공통 체력에 위임한다. */
UCLASS(Blueprintable, NotPlaceable)
class LANDSHIP_API ALSBasicProjectile : public AActor
{
	GENERATED_BODY()

  public:
	ALSBasicProjectile();
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetVelocity() const override
	{
		return FlightVelocity;
	}
	/** SpawnActorDeferred 이후 FinishSpawning 전에 한 번 호출한다. */
	bool Launch(const FLSDamageRequest& Request, AActor* Target, const FVector& Direction, float Speed, float Radius,
	            float Lifetime, UMaterialInterface* Material = nullptr);
	/** 발리스타: 지형·아군을 통과하고 진행 경로의 첫 적에게만 명중한다. */
	bool LaunchFirstEnemy(const FLSDamageRequest& Request, const FVector& Direction, float Speed, float Radius,
	                      float Lifetime, UMaterialInterface* Material = nullptr);
	UPROPERTY(BlueprintAssignable, Category = "Landship|Projectile")
	FLSProjectileDamageApplied OnDamageApplied;

  protected:
	virtual void BeginPlay() override;

  private:
	void ResolveHit(const FHitResult& Hit);
	bool InitializeFlight(const FLSDamageRequest& Request, const FVector& Direction, float Speed, float Radius,
	                      float Lifetime, UMaterialInterface* Material);
	bool FindFirstEnemyHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Projectile")
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Projectile")
	TObjectPtr<UStaticMeshComponent> Shaft;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Projectile")
	TObjectPtr<UStaticMeshComponent> Tip;
	UPROPERTY(Transient)
	FLSDamageRequest ShotRequest;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> OriginalSource;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> IntendedTarget;
	FVector FlightVelocity = FVector::ZeroVector;
	float RemainingFlightTime = 0;
	bool bLaunched = false;
	bool bResolved = false;
	bool bHitsFirstEnemy = false;
};
