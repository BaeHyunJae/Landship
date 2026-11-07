#pragma once

#include "CoreMinimal.h"
#include "Combat/LSPoolStorage.h"
#include "Troops/LSTroopUnit.h"
#include "Subsystems/WorldSubsystem.h"
#include "LSTroopPoolSubsystem.generated.h"

struct LANDSHIP_API FLSTroopLease
{
	TWeakObjectPtr<ALSTroopUnit> Unit;
	uint64 Serial = 0;
};

USTRUCT()
struct FLSTroopPoolBucket
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TSet<TObjectPtr<ALSTroopUnit>> Owned;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ALSTroopUnit>> Inactive;
};

USTRUCT()
struct FLSTroopPoolClassBuckets
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TMap<ELSTroopType, FLSTroopPoolBucket> Types;
};

/** 클래스·병종별 병력 풀. 공통 대여 검사를 공유하고 병력 lifecycle이 실제 상태를 초기화한다. */
UCLASS()
class LANDSHIP_API ULSTroopPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
  public:
	/** 현재 기본 ALSTroopUnit만 지원한다. 클래스별 보관 경계는 유지한다. 반환 전에 피해/타격 처리가 끝나야 한다. */
	FLSTroopLease AcquireUnit(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type, ELSFaction Faction,
	                          const FTransform& Transform, AActor* Owner = nullptr);
	bool ReleaseUnit(const FLSTroopLease& Lease);
	bool IsAcquired(const ALSTroopUnit* Unit, uint64 Serial) const;
	/** 지정 클래스·병종의 비활성 수를 목표로 한다. 상한 이내 실제 준비 수를 반환한다. */
	int32 PrewarmPool(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type, int32 DesiredInactiveCount);
	virtual void Deinitialize() override;
#if !UE_BUILD_SHIPPING
	void WriteDiagnostics() const;
#endif
  protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

  private:
	bool CanUse(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type) const;
	ALSTroopUnit* SpawnInactive(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type);
	void Retire(ALSTroopUnit* Unit);
	UFUNCTION()
	void OnUnitDestroyed(AActor* Actor);
	UPROPERTY(Transient)
	TMap<TSubclassOf<ALSTroopUnit>, FLSTroopPoolClassBuckets> Buckets;
	FLSPoolLeases Leases;
	bool bShuttingDown = false;
#if !UE_BUILD_SHIPPING
	int32 Active = 0;
	int32 PeakActive = 0;
	int64 Created = 0;
	int64 Reused = 0;
	int64 Acquired = 0;
	int64 Released = 0;
	int64 Retired = 0;
#endif
};
