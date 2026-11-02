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
	/** Deferred 생성 중 한 번 호출한다. 목표는 발사 때만 검사하며 경로의 첫 피해 허용 대상에 명중한다. */
	bool Launch(const FLSDamageRequest& Request, AActor* Target, const FVector& Direction, float Speed, float Radius,
	            float Lifetime, UMaterialInterface* Material = nullptr);
	/** 발리스타: 발사자·지형을 통과한다. 같은 진영은 오폭 금지 시 통과하며 경로의 첫 피해 허용 대상에 명중한다. */
	bool LaunchInDirection(const FLSDamageRequest& Request, const FVector& Direction, float Speed, float Radius,
	                      float Lifetime, UMaterialInterface* Material = nullptr);
	/** 발사자는 제외하고 지형과 같은 진영을 포함한 첫 실제 몸체에서 멈춘다. 피해 허용 여부는 공통 체력 정책을 따른다. */
	bool LaunchFirstBlocking(const FLSDamageRequest& Request, const FVector& Direction, float Speed, float Radius,
	                         float Lifetime, UMaterialInterface* Material = nullptr);
	/** 실제 감소량이 양수인 명중의 대상과 피해다. 같은 진영과 최초 목표 외의 적도 포함한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Projectile")
	FLSProjectileDamageApplied OnDamageApplied;

  protected:
	virtual void BeginPlay() override;

  private:
	void ResolveHit(const FHitResult& Hit);
	bool InitializeFlight(const FLSDamageRequest& Request, const FVector& Direction, float Speed, float Radius,
	                      float Lifetime, UMaterialInterface* Material);
	bool FindFirstDamageableHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	FLSDamageRequest MakeHitRequest() const;
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
	FVector FlightVelocity = FVector::ZeroVector;
	float RemainingFlightTime = 0;
	bool bLaunched = false;
	bool bResolved = false;
	bool bHitsFirstBlocking = false;
};
