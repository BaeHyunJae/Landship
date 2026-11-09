#pragma once

#include "CoreMinimal.h"
#include "Combat/LSPoolStorage.h"
#include "Subsystems/WorldSubsystem.h"
#include "LSProjectilePoolSubsystem.generated.h"

class ALSBasicProjectile;

/** 비소유 대여 핸들. 비동기 작업은 획득 당시 Serial을 보관하고 그대로 반환해야 한다. */
struct LANDSHIP_API FLSProjectileLease
{
	TWeakObjectPtr<ALSBasicProjectile> Projectile;
	uint64 Serial = 0;
};

USTRUCT()
struct FLSProjectilePoolBucket
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TSet<TObjectPtr<ALSBasicProjectile>> Owned;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ALSBasicProjectile>> Inactive;
};

#if !UE_BUILD_SHIPPING
struct FLSProjectilePoolCounters
{
	int32 Active = 0;
	int32 PeakActive = 0;
	int64 Created = 0;
	int64 Reused = 0;
	int64 Acquired = 0;
	int64 Released = 0;
	int64 Retired = 0;
};
#endif

/** 클래스별 화살 풀. 설정·비행·명중은 투사체가, 대여·보관·소유는 이 서브시스템이 맡는다. */
UCLASS()
class LANDSHIP_API ULSProjectilePoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
  public:
	/** 비활성 수를 목표로 사전 생성한다. 보관 상한까지만 준비하며 실제 비활성 수를 반환한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Projectile Pool")
	int32 PrewarmPool(TSubclassOf<ALSBasicProjectile> ProjectileClass, int32 DesiredInactiveCount);
	/** 지원 클래스·같은 월드의 소유자만 허용한다. 실패/활성화 중 반환 시 빈 핸들을 반환한다.
	 * 획득은 아직 발사가 아니다. 호출자는 Launch를 한 번 호출하고 실패하면 같은 핸들을 반환한다. */
	FLSProjectileLease AcquireProjectile(TSubclassOf<ALSBasicProjectile> ProjectileClass, const FTransform& Transform,
	                                     AActor* Owner = nullptr, APawn* Instigator = nullptr);
	/** 발급한 활성 대여만 소비한다. 오래된 세대·중복·다른 월드·직접 생성 객체는 false다. */
	bool ReleaseProjectile(const FLSProjectileLease& Lease);
	bool IsAcquired(const ALSBasicProjectile* Projectile, uint64 Serial) const;
	/** 전투 복원 전에 현재 활성 대여를 모두 반환한다. 반환 중 새 대여는 거부한다. */
	void ReleaseAll();
	virtual void Deinitialize() override;
#if !UE_BUILD_SHIPPING
	void WriteDiagnostics() const;
	void ResetDiagnostics();
#endif
  protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

  private:
	bool CanUseClass(TSubclassOf<ALSBasicProjectile> ProjectileClass) const;
	ALSBasicProjectile* SpawnInactive(TSubclassOf<ALSBasicProjectile> ProjectileClass);
	void Retire(ALSBasicProjectile* Projectile);
	UFUNCTION()
	void OnProjectileDestroyed(AActor* Actor);
	UPROPERTY(Transient)
	TMap<TSubclassOf<ALSBasicProjectile>, FLSProjectilePoolBucket> Buckets;
	FLSPoolLeases Leases;
	bool bShuttingDown = false;
	bool bReturningAll = false;
#if !UE_BUILD_SHIPPING
	TMap<TWeakObjectPtr<UClass>, FLSProjectilePoolCounters> Counters;
#endif
};
