#pragma once

#include "GameFramework/GameModeBase.h"
#include "Match/LSAutoBattleScenario.h"
#include "Match/LSMeleeApproachPlanner.h"
#include "LSAutoBattleGameMode.generated.h"

class ALSMobileCastle;
class AAIController;
struct FLSAutoBattlePreset;

UENUM(BlueprintType)
enum class ELSAutoBattlePhase : uint8
{
	Loading,
	Preparation,
	Fighting,
	Victory,
	Defeat,
	Error
};

UENUM(BlueprintType)
enum class ELSAutoBattleRequest : uint8
{
	Success,
	WrongPhase,
	InvalidData,
	InvalidPlacement,
	InsufficientBudget,
	PopulationFull,
	InvalidTarget,
	Unavailable
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLSAutoBattleChanged);

/** 전용 맵에서 준비·자동 전투·결과를 관리한다. 기존 RTS GameMode의 영구 종료 상태를 재사용하지 않는다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleGameMode : public AGameModeBase
{
	GENERATED_BODY()
  public:
	ALSAutoBattleGameMode();
	virtual void StartPlay() override;
	ELSAutoBattlePhase GetPhase() const
	{
		return Phase;
	}
	int32 GetBudget() const
	{
		return RemainingBudget;
	}
	int32 GetPopulation() const
	{
		return Formation.Num();
	}
	int32 GetBattleId() const
	{
		return BattleId;
	}
	const FString& GetFailure() const
	{
		return Failure;
	}
	const TArray<FLSAutoBattlePlacement>& GetFormation() const
	{
		return Formation;
	}
	ALSAutoBattleScenario* GetScenario() const
	{
		return Scenario;
	}
	ALSMobileCastle* GetAllyCastle() const
	{
		return AllyCastle;
	}
	ALSMobileCastle* GetEnemyCastle() const
	{
		return EnemyCastle;
	}
	ELSCastleWeapon GetWeapon() const
	{
		return SelectedWeapon;
	}
	ALSUnit* GetPlacementActor(int32 Id) const;
	int32 FindPlacementId(const AActor* Actor) const;
	/** 지면 좌표를 검사하고 생성 성공 시에만 예산과 편성을 확정한다. */
	ELSAutoBattleRequest PlaceUnit(ELSTroopType Type, const FVector& Ground, int32& OutId,
	                               ELSFaction Faction = ELSFaction::Ally);
	ELSAutoBattleRequest MoveUnit(int32 Id, const FVector& Ground);
	ELSAutoBattleRequest RemoveUnit(int32 Id);
	ELSAutoBattleRequest ResetFormation(ELSFaction Faction = ELSFaction::Ally);
	ELSAutoBattleRequest MoveCastle(const FVector& Ground, ELSFaction Faction = ELSFaction::Ally);
	ELSAutoBattleRequest SelectWeapon(ELSCastleWeapon Weapon, ELSFaction Faction = ELSFaction::Ally);
	ELSAutoBattleRequest StartBattle();
	/** 결과에서 복원을 요청한다. 성공은 요청 수락이며 Loading 뒤 준비 또는 Error로 알린다. true는 직전 편성, false는 빈
	 * 편성이다. */
	ELSAutoBattleRequest RetryBattle(bool bKeepFormation = true);
	bool CanPlace(ELSTroopType Type, const FVector& Ground, int32 IgnoreId, FVector& OutGround,
	              ELSFaction Faction = ELSFaction::Ally) const;
	bool CanPlaceCastle(const FVector& Ground, FVector& OutGround, ELSFaction Faction = ELSFaction::Ally) const;
	ELSAutoBattleRequest SetPreparationLimits(int32 Budget, int32 PopulationLimit);
	FLSAutoBattlePreset GetPreset() const;
	/** 실패하면 현재 액터와 편성을 유지한다. 성공 시 새 준비 액터로 교체한다. */
	ELSAutoBattleRequest ApplyPreset(const FLSAutoBattlePreset& Preset);
	bool SavePreset(const FString& Name, bool bOverwrite, FString& Error) const;
	bool LoadPreset(const FString& Name, FString& Error);
	UPROPERTY(BlueprintAssignable)
	FLSAutoBattleChanged OnChanged;

  protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle", meta = (ClampMin = "0.05", Units = "s"))
	float DecisionInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle", meta = (ClampMin = "0.05", Units = "s"))
	float RepathInterval = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle", meta = (ClampMin = "1", Units = "cm"))
	float RepathDistance = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle", meta = (ClampMin = "0", Units = "cm"))
	float ApproachPadding = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Melee")
	FLSMeleeApproachSettings MeleeApproach;

  private:
	struct FLSAutoBattleAgent
	{
		TWeakObjectPtr<ALSUnit> Unit;
		TWeakObjectPtr<ALSUnit> Target;
		FVector Goal = FVector::ZeroVector;
		double NextRepath = 0;
	};
	void InitializeScenario();
	bool Rebuild(const TArray<FLSAutoBattlePlacement>& NewFormation, bool bRestoreValidatedGround);
	ALSUnit* SpawnUnit(FLSAutoBattlePlacement& Placement, ELSFaction Faction, bool bRestoreValidatedGround = false);
	ALSMobileCastle* SpawnCastle(const FVector& Ground, ELSFaction Faction, bool bRestoreValidatedGround = false);
	void Freeze(ALSUnit* Unit);
	void DestroyBattle();
	void ReleaseUnitActor(ALSUnit* Unit);
	void ReleaseUnitController(AAIController* Controller);
	void AutoTick();
	void UpdateAgent(FLSAutoBattleAgent& Agent, const TArray<ALSUnit*>& Opponents, const TArray<ALSUnit*>& Allies);
	void FireCastle(ALSMobileCastle* Castle, ELSCastleWeapon Weapon, const TArray<ALSUnit*>& Opponents);
	void CollectOutcome(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void OnActorSpawned(AActor* Actor);
	void Fail(const FString& Reason);
	bool ResolveGround(const FVector& Ground, float Radius, float Height, const AActor* Ignore, bool bDeployment,
	                   FVector& OutGround, bool bRestoreValidatedGround = false) const;
	UPROPERTY(Transient)
	TObjectPtr<ALSAutoBattleScenario> Scenario;
	UPROPERTY(Transient)
	TObjectPtr<ALSMobileCastle> AllyCastle;
	UPROPERTY(Transient)
	TObjectPtr<ALSMobileCastle> EnemyCastle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ALSUnit>> Units;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ALSUnit>> PlacementActors;
	TArray<TWeakObjectPtr<AActor>> Projectiles;
	TArray<TWeakObjectPtr<AAIController>> Controllers;
	TArray<FLSAutoBattleAgent> Agents;
	FLSMeleeApproachPlanner MeleePlanner;
	TArray<FLSAutoBattlePlacement> Formation;
	FVector CastleGround = FVector::ZeroVector;
	ELSCastleWeapon SelectedWeapon = ELSCastleWeapon::Cannon;
	ELSAutoBattlePhase Phase = ELSAutoBattlePhase::Loading;
	FString Failure;
	int32 RemainingBudget = 0;
	int32 NextPlacementId = 1;
	int32 NextEnemyId = -1;
	TArray<TWeakObjectPtr<AActor>> PlacementIgnoredActors;
	int32 BattleId = 0;
	double NavigationDeadline = 0;
	bool bHasPreparedFormation = false;
	FTimerHandle InitializationTimer;
	FTimerHandle DecisionTimer;
	FDelegateHandle OutcomeHandle;
	FDelegateHandle SpawnHandle;
	bool bChanging = false;
};
