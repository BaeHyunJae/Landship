#pragma once

#include "GameFramework/Actor.h"
#include "Combat/LSHealthTypes.h"
#include "Combat/LSBasicAttackComponent.h"
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
	/** Deferred 생성 중에만 시각 형태를 정한다. 충돌과 비행 수치는 바꾸지 않는다. */
	void SetVisualStyle(ELSProjectileVisualStyle Style);
	ELSProjectileVisualStyle GetVisualStyle() const { return VisualStyle; }
	bool IsInFlight() const { return bLaunched && !bResolved && !IsActorBeingDestroyed(); }
	FVector GetFlightOrigin() const { return FlightOrigin; }
	FVector GetFlightDirection() const { return FlightVelocity.GetSafeNormal(); }
	float GetRemainingFlightTime() const { return RemainingFlightTime; }
	bool GetFlightFaction(ELSFaction& OutFaction) const
	{
		if (!ShotRequest.bHasSourceFaction) return false;
		OutFaction = ShotRequest.SourceFaction;
		return true;
	}
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetVelocity() const override
	{
		return FlightVelocity;
	}
	/** Deferred 생성 중 한 번 호출한다. 목표는 발사 때만 검사한다. 첫 피해 허용 몸체 또는 실제 지형에서 멈춘다. */
	bool Launch(const FLSDamageRequest& Request, AActor* Target, const FVector& Direction, float Speed, float Radius,
	            float Lifetime, UMaterialInterface* Material = nullptr);
	/** 발리스타: 발사자는 통과하고 첫 피해 허용 몸체 또는 실제 지형에서 멈춘다. 같은 진영은 오폭 금지 시 통과한다. */
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
	bool FindFirstHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
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
	FVector FlightOrigin = FVector::ZeroVector;
	ELSProjectileVisualStyle VisualStyle = ELSProjectileVisualStyle::Arrow;
	float RemainingFlightTime = 0;
	bool bLaunched = false;
	bool bResolved = false;
	bool bHitsFirstBlocking = false;
	bool bStopsAtWorldGeometry = false;
};
