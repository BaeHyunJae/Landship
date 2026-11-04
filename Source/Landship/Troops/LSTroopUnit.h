#pragma once

#include "Troops/LSUnit.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSTroopUnit.generated.h"

class ULSTroopCombatComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAnimSequence;
class ULSTroopPoolSubsystem;
struct FLSDamageResult;

UENUM(BlueprintType)
enum class ELSTroopType : uint8
{
	Spearman,
	Archer,
	Cavalry,
	Medic
};

/** 공통 전투·이동을 재사용하고 병종별 데이터 행과 임시 외형만 선택한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSTroopUnit : public ALSUnit
{
	GENERATED_BODY()

  public:
	ALSTroopUnit();
	virtual bool SupportsBasicAttack() const override { return TroopType != ELSTroopType::Medic; }
	static constexpr int32 GetTroopTypeCount() { return static_cast<int32>(ELSTroopType::Medic) + 1; }
	bool IsPoolManaged() const { return PoolOwner.IsValid(); }
	uint64 GetActivationSerial() const { return ActivationSerial; }
	bool CanReturnToPool() const;
	/** 풀에서 생성한 기본 병력에만 연결한다. 일반 배치 액터는 자동 편입하지 않는다. */
	bool PrepareForPool(ULSTroopPoolSubsystem* Pool);
	bool ActivateFromPool(ULSTroopPoolSubsystem* Pool, uint64 Serial, ELSFaction NewFaction,
	    const FTransform& Transform, AActor* NewOwner);
	bool DeactivateForPool(ULSTroopPoolSubsystem* Pool, uint64 Serial);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Landship|Troops")
	TObjectPtr<ULSTroopCombatComponent> CombatBehavior;
	void SetTacticalLabel(const FString& State);
	void SetLanceLowered(bool bLowered);
	UPROPERTY(VisibleAnywhere, Category = "Landship|Troops")
	TObjectPtr<UStaticMeshComponent> Lance;
	virtual void OnConstruction(const FTransform& Transform) override;
	UFUNCTION(BlueprintPure, Category = "Landship|Troops")
	ELSTroopType GetTroopType() const
	{
		return TroopType;
	}
	/** 생성 완료·전투 준비 전에 지정한다. 전투 중 변경과 잘못된 병종은 거부한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Troops")
	bool SetInitialTroopType(ELSTroopType InType);
	UStaticMeshComponent* GetTroopMarker() const
	{
		return TroopMarker;
	}
	static FName GetStatsRowName(ELSTroopType Type);

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* Property) const override;
#endif

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

  private:
	void RefreshDefinition();
	void InitializeCombatBehavior();
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Behavior")
	TMap<ELSTroopType, TSubclassOf<ULSTroopCombatComponent>> CombatBehaviorClasses;
	void InitializeAnimations();
	void PlayIdleAnimation();
	void RestoreMarkerColor();
	UFUNCTION()
	void OnTroopDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnTroopAttackExecuted(const FLSAttackExecution& Execution);
	UFUNCTION()
	void OnTroopDied(const FLSDamageResult& Damage);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Troops",
	          meta = (ExposeOnSpawn = "true", AllowPrivateAccess = "true"))
	ELSTroopType TroopType = ELSTroopType::Spearman;
	/** 병종 수치는 이 테이블에서 조정한다. 초기화 실패는 부모의 공통 규칙대로 차단한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	TSoftObjectPtr<UDataTable> TroopStatsTable;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	TSoftObjectPtr<UMaterialInterface> PrototypeMaterial;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Troops")
	TObjectPtr<UStaticMeshComponent> TroopMarker;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Troops")
	TObjectPtr<UTextRenderComponent> TroopLabel;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SpearShape;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ArcherShape;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CavalryShape;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	FLSProjectileAttackSettings RangedAttack;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Animation")
	TSoftObjectPtr<UAnimSequence> MeleeIdleAnimation;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Animation")
	TSoftObjectPtr<UAnimSequence> MeleeAttackAnimation;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Animation")
	TSoftObjectPtr<UAnimSequence> ArcherIdleAnimation;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Animation")
	TSoftObjectPtr<UAnimSequence> ArcherAttackAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveIdleAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAttackAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MarkerMaterialSource;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Feedback", meta = (ClampMin = "0.01", Units = "s"))
	float HitFlashDuration = 0.12f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops|Feedback")
	FLinearColor HitFlashColor = FLinearColor(1.0f, 0.9f, 0.25f);
	FLinearColor MarkerColor = FLinearColor::White;
	TWeakObjectPtr<AActor> VisualAttackTarget;
	FTimerHandle AttackAnimationTimer;
	FTimerHandle HitFlashTimer;
	bool bTroopTypeLocked = false;
	UPROPERTY(Transient)
	TWeakObjectPtr<ULSTroopPoolSubsystem> PoolOwner;
	uint64 ActivationSerial = 0;
	FTimerHandle PoolDeathTimer;
	bool bPoolRemovesOnDeath = false;
};
