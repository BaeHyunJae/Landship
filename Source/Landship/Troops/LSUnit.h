#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Combat/LSCombatStatRows.h"
#include "Combat/LSCombatStats.h"
#include "Combat/LSFactionInterface.h"
#include "LSUnit.generated.h"

/** 초기 수치·진영·체력을 준비하고 사망 시 필수 이동·충돌 처리를 맡는다. 공격 실행은 별도다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSUnit : public ACharacter, public ILSFactionInterface
{
	GENERATED_BODY()

  public:
	ALSUnit(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	/** 명령과 공격을 영구 정지하되 낙하·착지와 명중에 의한 물리 반응은 유지한다. */
	void EndBattleActions();
	virtual bool SupportsBasicAttack() const { return true; }
	bool IsGameplayStaged() const { return bGameplayStaged; }
	/** 네이티브 생산 액터를 컴포넌트 등록 전에 격리한다. 생산 컴포넌트만 해제한다. */
	void StageForProduction();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSSelectableComponent> Selection;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSOrderComponent> Orders;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSVisionParticipantComponent> Vision;

	virtual ELSFaction GetFaction() const override
	{
		return Faction;
	}
	/** 전투 준비 시작 전에 지정한다. C++ 지연 생성에서도 쓸 수 있으며 시작 후 변경은 거부한다. */
	void SetInitialFaction(ELSFaction InFaction);
	FString GetCombatConfigurationError() const;
	virtual void Restart() override;
	virtual void Landed(const FHitResult& Hit) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual bool CanEditChange(const FProperty* Property) const override;
#endif

	/** 초기화 전이나 실패 시에는 출력값을 변경하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool TryGetStats(FLSUnitStatRow& OutStats) const;

	/** 수치 읽기 성공만 나타낸다. 전투 준비 여부는 체력 컴포넌트에서 확인한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool IsStatsInitialized() const
	{
		return StatsInitResult == ELSCombatStatsInitResult::Success;
	}

	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	ELSCombatStatsInitResult GetStatsInitResult() const
	{
		return StatsInitResult;
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSBasicAttackComponent> BasicAttack;

  protected:
	virtual void BeginPlay() override;
	/** 풀의 숨김·충돌 중지 상태에서만 호출한다. 새 생명의 수치와 진영을 다시 준비한다. */
	bool PreparePooledCombat(ELSFaction NewFaction);
	bool SuspendCombatForPool();
	virtual void ActivateProducedUnit();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSHealthComponent> Health;

	/** 인간형에서만 켠다. 메시·Physics Asset이 필요하며 사망한 메시는 LSGround와만 충돌한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Death")
	uint8 bUseRagdoll : 1 = false;

	/** 수치 자체는 테이블에서 조정하고 여기서는 사용할 행만 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Stats", meta = (RowType = "/Script/Landship.LSUnitStatRow"))
	FDataTableRowHandle StatsRow;

  private:
	friend class ULSTroopProductionComponent;
	void ReleaseProductionStaging();
	bool bGameplayStaged = false;
	TArray<TWeakObjectPtr<class UPrimitiveComponent>> ProductionOverlapComponents;
	void StopCombatMovement();
	void NeutralizeCombat();
	void PrepareForDeath();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Combat",
	          meta = (ExposeOnSpawn = "true", AllowPrivateAccess = "true"))
	ELSFaction Faction = ELSFaction::Ally;

	bool bFactionLocked = false;
	bool bBattleActionsEnded = false;

	/** 현재 전투 상태와 구분되는 초기 수치 복사본이다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	FLSUnitStatRow Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	ELSCombatStatsInitResult StatsInitResult = ELSCombatStatsInitResult::NotAttempted;
};
