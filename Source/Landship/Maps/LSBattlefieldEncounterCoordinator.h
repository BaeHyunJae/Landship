#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSBattlefieldEncounterCoordinator.generated.h"

class ALSMine;
class ULSHealthComponent;
struct FLSDamageResult;
struct FLSMineExplosion;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSEncounterRegion
{
	GENERATED_BODY()
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	FName RegionId;
	/** 월드 좌표의 XY 진입 영역이다. 높이는 진입 판정에 쓰지 않는다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<FBox> EntryBounds;
	/** 이 지역에 배정한 전투 액터의 루트가 머물 수 있는 월드 XY 영역이다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<FBox> PlacementBounds;
	/** 지정하면 휴면 Assault의 배치는 이 영역으로 더 좁힌다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<FBox> AssaultPlacementBounds;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<TObjectPtr<AActor>> AlarmTargets;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<TObjectPtr<ALSMine>> Mines;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<TObjectPtr<AActor>> AssaultMembers;
};

/** 명시적으로 지정된 지역만 활성화한다. 이웃 지역이나 일반 지원 요청으로 전파하지 않는다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSBattlefieldEncounterCoordinator : public AActor
{
	GENERATED_BODY()
public:
	ALSBattlefieldEncounterCoordinator();
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Encounter")
	TArray<FLSEncounterRegion> Regions;
	UFUNCTION(BlueprintPure, Category = "Encounter")
	bool IsRegionActivated(FName RegionId) const { return ActivatedRegions.Contains(RegionId); }
	UFUNCTION(BlueprintPure, Category = "Encounter")
	FString GetConfigurationError() const;
	/** 키는 액터의 GetFName(), 값은 적용할 월드 변환이다. 현재 배치는 변경하지 않는다. */
	bool ValidatePresetTransforms(const TMap<FName, FTransform>& Transforms, FString& OutError) const;
	static FString GetDormantMembershipError(const AActor* Member);
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
private:
	void RegisterListeners();
	void UnregisterListeners();
	void OnDamageCommitted(const FLSDamageResult& Damage);
	void OnMineExploded(const FLSMineExplosion& Explosion);
	void ActivateRegion(FName RegionId);
	bool AreActionsStopped() const;
	FString Validate(const TMap<FName, FTransform>* Transforms, bool bCheckWorldMembership = true) const;
	TMap<int64, FName> DamageRegions;
	TMap<uint32, FName> MineRegions;
	TArray<TWeakObjectPtr<ULSHealthComponent>> BoundHealth;
	TArray<TWeakObjectPtr<ALSMine>> BoundMines;
	TSet<FName> ActivatedRegions;
	bool bConfigurationValid = false;
};
