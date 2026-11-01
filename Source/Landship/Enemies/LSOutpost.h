#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSCombatStatRows.h"
#include "Combat/LSCombatStats.h"
#include "Combat/LSFactionInterface.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "LSOutpost.generated.h"

class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSOnOutpostCaptured, const FLSOutpostCaptureSnapshot&, Capture);

/** 초기 수치·진영·체력과 사망 처리를 소유한다. 공격 실행은 별도 기능에서 담당한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSOutpost : public AActor, public ILSFactionInterface
{
	GENERATED_BODY()

  public:
	ALSOutpost();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSSelectableComponent> Selection;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSOrderComponent> Orders;

	virtual ELSFaction GetFaction() const override
	{
		return Faction;
	}
	/** 전투 준비가 시작되기 전에 지정한다. C++ 지연 생성에서도 사용할 수 있다. */
	void SetInitialFaction(ELSFaction InFaction);
	FString GetCombatConfigurationError() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Outpost")
	bool HasBeenCaptured() const { return bCaptured; }
	UFUNCTION(BlueprintPure, Category = "Landship|Outpost")
	bool IsArmed() const { return bArmed; }
	UFUNCTION(BlueprintPure, Category = "Landship|Outpost")
	float GetDefenseRadius() const { return DefenseRadius; }
	UFUNCTION(BlueprintPure, Category = "Landship|Outpost")
	bool TryGetCapture(FLSOutpostCaptureSnapshot& Out) const;

	/** 체력 0 사실과 필수 정리가 확정된 뒤 한 번 발생한다. 제거 후 핵심 함락은 월드 조회를 쓴다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Outpost")
	FLSOnOutpostCaptured OnOutpostCapturedEvent;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** 초기화 전이나 실패 시에는 출력값을 변경하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool TryGetStats(FLSOutpostStatRow& OutStats) const;

	/** 초기 수치 읽기의 성공 여부다. 체력 준비 여부는 체력 컴포넌트에서 조회한다. */
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 피해량 0과 무장 없음을 구분한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Outpost")
	uint8 bArmed : 1 = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Outpost", meta = (ClampMin = "0", Units = "cm"))
	float DefenseRadius = 1200.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSHealthComponent> Health;

	/** 수치 자체는 테이블에서 조정하고 여기서는 사용할 행만 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Stats", meta = (RowType = "/Script/Landship.LSOutpostStatRow"))
	FDataTableRowHandle StatsRow;

	/** 전투 대상 탐색에 사용하는 건물 충돌 영역이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<UBoxComponent> Collision;

  private:
	/** 실패 시 실행만 차단하며 사망 알림·랙돌·제거를 수행하지 않는다. */
	void NeutralizeCombat();
	void PrepareForDeath();
	void BroadcastCapture();
	FLSOutpostCaptureSnapshot Capture;
	FTimerHandle CaptureTimer;
	bool bCaptured = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Combat",
	          meta = (ExposeOnSpawn = "true", AllowPrivateAccess = "true"))
	ELSFaction Faction = ELSFaction::Enemy;

	bool bFactionLocked = false;

	/** 현재 전투 상태와 구분되는 초기 수치 복사본이다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	FLSOutpostStatRow Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	ELSCombatStatsInitResult StatsInitResult = ELSCombatStatsInitResult::NotAttempted;
};
