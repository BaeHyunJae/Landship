#pragma once

#include "GameFramework/Actor.h"
#include "Combat/LSHealthTypes.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSBasicProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ULSProjectilePoolSubsystem;
class ULSBasicAttackComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSProjectileDamageApplied, AActor*, Target, float, AppliedDamage);
DECLARE_MULTICAST_DELEGATE_TwoParams(FLSProjectileDamageAppliedNative, AActor*, float);

/** 발사 시점의 피해·진영을 보관하는 직선 비유도 투사체다. 피해 정책은 공통 체력에 위임한다. */
UCLASS(Blueprintable, NotPlaceable)
class LANDSHIP_API ALSBasicProjectile : public AActor
{
	GENERATED_BODY()

  public:
	ALSBasicProjectile();
	bool IsPoolManaged() const
	{
		return PoolOwner.IsValid();
	}
	/** 파생 클래스는 모든 추가 상태·타이머를 lifecycle 함수에서 정리한 경우에만 명시적으로 허용한다. */
	virtual bool SupportsPooling() const;
	/** 풀의 deferred 생성 중에만 호출한다. 직접 생성·맵 배치 객체의 사후 편입은 지원하지 않는다. */
	bool PrepareForPool(ULSProjectilePoolSubsystem* Pool);
	/** 실제 발사는 Launch가 수행한다. 활성화 중 수명이 바뀌면 실패한다. */
	virtual bool ActivateFromPool(ULSProjectilePoolSubsystem* Pool, uint64 Serial,
	    const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator);
	/** 풀에서 대여를 소비한 뒤 호출한다. 파생 클래스는 추가 작업을 취소하고 Super를 호출해야 한다. */
	virtual bool DeactivateForPool(ULSProjectilePoolSubsystem* Pool, uint64 Serial);
	uint64 GetActivationSerial() const { return ActivationSerial; }
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
	/** 일반 생성은 deferred 중, 풀 대여는 활성화 뒤 한 번 호출한다. 비유도 비행하며 실제 지형에서 멈춘다. */
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
	FLSProjectileDamageAppliedNative OnDamageAppliedNative;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void LifeSpanExpired() override;

  private:
	void FinishFlight();
	void ResetForPool();
	bool IsCurrentFlight(uint64 Serial) const;
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
	UPROPERTY(Transient)
	TWeakObjectPtr<ULSBasicAttackComponent> SourceAttack;
	uint64 SourceLifeSerial = 0;
	bool bHasSourceLife = false;
	FVector FlightVelocity = FVector::ZeroVector;
	FVector FlightOrigin = FVector::ZeroVector;
	ELSProjectileVisualStyle VisualStyle = ELSProjectileVisualStyle::Arrow;
	float RemainingFlightTime = 0;
	bool bLaunched = false;
	bool bResolved = false;
	bool bHitsFirstBlocking = false;
	UPROPERTY(Transient)
	TWeakObjectPtr<ULSProjectilePoolSubsystem> PoolOwner;
	uint64 ActivationSerial = 0;
	FTimerHandle PoolLifetimeTimer;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlightMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlightMaterialInstance;
	bool bStopsAtWorldGeometry = false;
};
