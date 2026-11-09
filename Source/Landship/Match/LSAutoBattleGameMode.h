#pragma once

#include "GameFramework/GameModeBase.h"
#include "Match/LSAutoBattleScenario.h"
#include "Match/LSAutoBattlePreset.h"
#include "Match/LSMeleeApproachPlanner.h"
#include "LSAutoBattleGameMode.generated.h"

class ALSAutoBattleCastle;
class AAIController;
class AStaticMeshActor;
class ULevelStreamingDynamic;
class UDecalComponent;
class UMaterialInterface;
class ALSCastleProjectile;

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
	PopulationFull,
	InvalidTarget,
	Unavailable
};

enum class ELSCastleField : uint8
{
	Swamp,
	Stim,
	Wall
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
	ALSAutoBattleCastle* GetAllyCastle() const
	{
		return AllyCastle;
	}
	ALSAutoBattleCastle* GetEnemyCastle() const
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
	TOptional<ELSCastleWeapon> GetEnemyAimingWeapon() const;
	ELSWeaponCastResult GetFieldStatus(ELSCastleField Field) const;
	ELSWeaponCastResult ValidateFieldAim(ELSCastleField Field, const FVector& Aim) const;
	ELSWeaponCastResult CastField(ELSCastleField Field, const FVector& Aim);
	float GetFieldCooldown(ELSCastleField Field) const;
	float GetFieldRadius() const { return FieldRadius; }
	bool GetWallPreview(const FVector& Aim, FTransform& OutTransform, FVector& OutExtent) const;
	struct FLSBattlefieldOption
	{
		const TCHAR* Label;
		const TCHAR* TerrainPackage;
	};
	static TConstArrayView<FLSBattlefieldOption> GetBattlefieldOptions();
	int32 GetCurrentBattlefieldIndex() const;
	/** 준비 단계에서 현재 레벨의 지형을 교체하고 새 준비 편성을 초기화한다. */
	bool SelectBattlefield(int32 Index);
	/** 전투 중 또는 결과에서 복원을 요청한다. 성공은 요청 수락이며 Loading 뒤 준비 또는 Error로 알린다.
	 * true는 아군 편성 유지, false는 아군 편성 초기화다. */
	ELSAutoBattleRequest RetryBattle(bool bKeepFormation = true);
	bool CanPlace(ELSTroopType Type, const FVector& Ground, int32 IgnoreId, FVector& OutGround,
	              ELSFaction Faction = ELSFaction::Ally) const;
	bool CanPlaceCastle(const FVector& Ground, FVector& OutGround, ELSFaction Faction = ELSFaction::Ally) const;
	FLSAutoBattlePreset GetPreset() const;
	/** 실패하면 현재 액터와 편성을 유지한다. 성공 시 새 준비 액터로 교체한다. */
	ELSAutoBattleRequest ApplyPreset(const FLSAutoBattlePreset& Preset);
	bool SavePreset(const FString& Name, bool bOverwrite, FString& Error) const;
	bool LoadPreset(const FString& Name, FString& Error);
	FString TakePresetLoadMessage();
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
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Enemy Castle", meta = (ClampMin = "0.1", Units = "s"))
	float EnemyAimTime = 0.85f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Enemy Castle", meta = (ClampMin = "0.1", Units = "s"))
	float EnemyWeaponRecovery = 1.25f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "1", Units = "cm"))
	float FieldRadius = 450.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "1", Units = "cm"))
	float FieldCastRange = 2200.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "0.1", Units = "s"))
	float FieldDuration = 6.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "0.1", Units = "s"))
	float FieldCooldown = 14.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "0.1", ClampMax = "1"))
	float SwampSpeed = 0.7f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "1"))
	float StimSpeed = 1.3f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields")
	TObjectPtr<UMaterialInterface> FieldDecalMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "0.1", Units = "s"))
	float WallDuration = 3.0f;
	UPROPERTY(EditDefaultsOnly, Category = "AutoBattle|Castle Fields", meta = (ClampMin = "1", Units = "cm"))
	FVector WallHalfExtent = FVector(475.0, 35.0, 125.0);

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
	ALSAutoBattleCastle* SpawnCastle(const FVector& Ground, ELSFaction Faction, bool bRestoreValidatedGround = false);
	void Freeze(ALSUnit* Unit);
	void DestroyBattle();
	void ReleaseUnitActor(ALSUnit* Unit);
	void ReleaseUnitController(AAIController* Controller);
	void AutoTick();
	void UpdateCastleFields();
	void ClearCastleFields();
	bool LaunchCastleField(ELSCastleField Field, const FVector& Aim);
	void OnCastleFieldImpact(const FVector& Impact, ELSCastleField Field, int32 SourceBattleId);
	bool SpawnCastleWall(const FTransform& Transform, const FVector& Extent);
	void UpdateAgent(FLSAutoBattleAgent& Agent, const TArray<ALSUnit*>& Opponents, const TArray<ALSUnit*>& Allies);
	/** 조준 또는 발사 중이면 true를 반환해 적 성의 이동 판단을 한 번 건너뛴다. */
	bool UpdateEnemyWeapons(const TArray<ALSUnit*>& Opponents);
	void DrawEnemyWeaponWarning() const;
	void CollectOutcome(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void OnActorSpawned(AActor* Actor);
	void Fail(const FString& Reason);
	bool ResolveGround(const FVector& Ground, float Radius, float Height, const AActor* Ignore, bool bDeployment,
	                   FVector& OutGround, bool bRestoreValidatedGround = false) const;
	UPROPERTY(Transient)
	TObjectPtr<ALSAutoBattleScenario> Scenario;
	UPROPERTY(Transient)
	TObjectPtr<ALSAutoBattleCastle> AllyCastle;
	UPROPERTY(Transient)
	TObjectPtr<ALSAutoBattleCastle> EnemyCastle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ALSUnit>> Units;
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<ALSUnit>> PlacementActors;
	TArray<TWeakObjectPtr<AActor>> Projectiles;
	TArray<TWeakObjectPtr<AAIController>> Controllers;
	TArray<FLSAutoBattleAgent> Agents;
	struct FLSEnemyWeaponAim
	{
		ELSCastleWeapon Weapon;
		FVector Aim;
		FVector CastleLocation;
		double FireTime;
	};
	TOptional<FLSEnemyWeaponAim> EnemyWeaponAim;
	double EnemyNextWeaponTime = 0;
	struct FLSCastleFieldState
	{
		FVector Center = FVector::ZeroVector;
		double Expires = 0;
		double ReadyAt = 0;
		TWeakObjectPtr<ALSCastleProjectile> Projectile;
		TWeakObjectPtr<UDecalComponent> Decal;
	};
	FLSCastleFieldState CastleFields[3];
	TWeakObjectPtr<AStaticMeshActor> SummonedWall;
	FLSMeleeApproachPlanner MeleePlanner;
	TArray<FLSAutoBattlePlacement> Formation;
	FVector CastleGround = FVector::ZeroVector;
	FVector DefaultEnemyCastleGround = FVector::ZeroVector;
	ELSCastleWeapon DefaultEnemyWeapon = ELSCastleWeapon::Cannon;
	struct FLSFlatTerrainState
	{
		TWeakObjectPtr<AStaticMeshActor> Actor;
		bool bHidden;
		bool bCollision;
		bool bNavigation;
	};
	TArray<FLSFlatTerrainState> FlatTerrain;
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> VariantTerrain;
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> UnloadingTerrain;
	int32 BattlefieldIndex = 0;
	bool bTerrainNavigationRebuildPending = false;
	ELSCastleWeapon SelectedWeapon = ELSCastleWeapon::Cannon;
	ELSAutoBattlePhase Phase = ELSAutoBattlePhase::Loading;
	FString Failure;
	TOptional<FLSAutoBattlePreset> PendingPreset;
	TOptional<FLSAutoBattlePreset> PreviousPreset;
	FString PresetLoadMessage;
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
