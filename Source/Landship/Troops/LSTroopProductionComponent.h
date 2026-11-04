#pragma once

#include "Components/ActorComponent.h"
#include "Troops/LSTroopUnit.h"
#include "LSTroopProductionComponent.generated.h"

UENUM(BlueprintType)
enum class ELSProductionResult : uint8
{
	Ready,
	SelectCastle,
	Cooldown,
	PopulationFull,
	NoSpace,
	Unavailable,
	Busy
};

/** 로컬 플레이어의 생산 쿨타임과 병력 인구수를 관리한다. UI와 단축키는 같은 요청을 사용한다. */
UCLASS(ClassGroup = (Landship))
class LANDSHIP_API ULSTroopProductionComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSTroopProductionComponent();
	ELSProductionResult GetStatus(ELSTroopType Type) const;
	ELSProductionResult Produce(ELSTroopType Type);
	float GetRemainingCooldown(ELSTroopType Type) const;
	float GetCooldownSeconds() const { return CooldownSeconds; }
	ALSTroopUnit* GetLastProducedUnit() const
	{
		return LastProducedUnit.Get();
	}
	/** 마지막 성공 생산 묶음이다. 실패한 요청은 이 기록을 바꾸지 않는다. */
	const TArray<TWeakObjectPtr<ALSTroopUnit>>& GetLastProducedBatch() const { return LastProducedBatch; }
	AActor* GetSelectedProducer() const;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Production", meta = (ClampMin = "0.01"))
	float CooldownSeconds = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Production", meta = (ClampMin = "1"))
	float MaxSearchDistance = 5000.0f;

  private:
	friend class FLSTroopProductionPlacementTest;
	friend class FLSTroopPairProductionTest;
	friend struct FLSTroopPairTestAccess;
	friend class FLSTroopProductionConnectivityTest;
	static FVector MakeSpawnDirection(const FVector& Forward, int32 Index, int32 Samples);
	static bool IsClearOfTravelPath(const FVector& Point, const TArray<FVector>& Path, float Clearance);
	void InitializePopulation();
	bool FindSpawnLocation(AActor* Castle, ELSTroopType Type, FVector& OutLocation,
		const TArray<FVector>& ReservedLocations = {}) const;
	bool FindSpawnPair(AActor* Castle, ELSTroopType Type, TArray<FVector>& OutLocations) const;
	void TrackUnit(ALSUnit* Unit);
	UFUNCTION()
	void OnUnitDied(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnUnitDestroyed(AActor* Actor);
	UPROPERTY(Transient)
	TMap<ELSTroopType, double> ReadyTimes;
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<ALSUnit>> PopulationUnits;
	TWeakObjectPtr<ALSTroopUnit> LastProducedUnit;
	TArray<TWeakObjectPtr<ALSTroopUnit>> LastProducedBatch;
	bool bInitialized = false;
	bool bProducing = false;
};
