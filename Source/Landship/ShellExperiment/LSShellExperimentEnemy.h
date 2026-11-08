#pragma once

#include "CoreMinimal.h"
#include "Troops/LSUnit.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSShellExperimentEnemy.generated.h"

class ULSEnemyBehaviorComponent;
class ULSShellReactiveComponent;
class UStaticMeshComponent;
class ALSShellWeakPoint;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
struct FLSAttackExecution;
struct FLSDamageResult;

UENUM(BlueprintType)
enum class ELSShellEnemyKind : uint8 { Assault, Carrier, Heavy };

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellExperimentEnemy : public ALSUnit
{
	GENERATED_BODY()
public:
	ALSShellExperimentEnemy();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") ELSShellEnemyKind EnemyKind = ELSShellEnemyKind::Assault;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") TObjectPtr<UDataTable> StatsTable;
	/** 비워 두면 EnemyKind와 같은 이름의 행을 사용한다. 공통 체력 초기화 전에 확정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") FName StatsRowName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") float CarrierExplosionRadius = 1000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Heavy") float HeavyMass = 12;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Heavy") float HeavyMovementResistance = 3;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Shell Experiment|Heavy") FLSProjectileAttackSettings HeavyProjectile;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Shell Experiment|Heavy") TSubclassOf<ALSShellWeakPoint> WeakPointClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Heavy") float WeakPointExplosionDamage = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Heavy") float WeakPointExplosionRadius = 1400;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Heavy") float WeakPointExplosionVelocity = 1400;
	bool IsHeavy() const { return EnemyKind == ELSShellEnemyKind::Heavy; }
	ALSShellWeakPoint* GetWeakPoint() const { return WeakPoint; }
	bool HasCommittedWeakPointDetonation() const { return bWeakPointDetonationCommitted; }
	/** 약점 사망 알림에서만 호출한다. 본체 피해 없이 값 복사와 기폭 예약만 수행한다. */
	void CommitWeakPointDetonation(ALSShellWeakPoint* Sender, const FLSDamageResult& Damage);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<ULSEnemyBehaviorComponent> Behavior;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<ULSShellReactiveComponent> Reactive;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UStaticMeshComponent> BodyVisual;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<class ULSShellMaterialStateComponent> MaterialState;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<class ULSShellSiegeController> Siege;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<class ULSShellObstacleResponseComponent> ObstacleResponse;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void ResolveStatsRow();
	void PrepareHeavyDefinition();
	void RefreshRoleVisuals();
	bool SpawnWeakPoint();
	void RemoveWeakPoint();
	void NotifyHeavyDefeatedOnce();
	UPROPERTY(Transient) TObjectPtr<ALSShellWeakPoint> WeakPoint;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY() TObjectPtr<USceneComponent> CannonAim;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CannonVisual;
	UPROPERTY() TObjectPtr<UTextRenderComponent> RoleLabel;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RoleMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrimMaterial;
	bool bWeakPointDetonationCommitted = false;
	bool bHeavyDefeatedNotified = false;
	UFUNCTION() void OnAttackExecuted(const FLSAttackExecution& Execution);
	UFUNCTION() void OnDied(const FLSDamageResult& Damage);
};
