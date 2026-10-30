#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Combat/LSCombatStatRows.h"
#include "Combat/LSCombatStats.h"
#include "Combat/LSFactionInterface.h"
#include "LSCastle.generated.h"

class UBoxComponent;

/** 초기 수치·진영·체력과 사망 처리를 소유한다. 공격 실행은 별도 기능에서 담당한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSCastle : public APawn, public ILSFactionInterface
{
	GENERATED_BODY()

  public:
	ALSCastle();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSSelectableComponent> Selection;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSOrderComponent> Orders;

	virtual UPawnMovementComponent* GetMovementComponent() const override;
	bool RequestMove(const FVector& Destination);
	bool IsMoving() const;
	void StopMoving();
	virtual ELSFaction GetFaction() const override
	{
		return Faction;
	}
	/** 전투 준비가 시작되기 전에 지정한다. C++ 지연 생성에서도 사용할 수 있다. */
	void SetInitialFaction(ELSFaction InFaction);
	FString GetCombatConfigurationError() const;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** 초기화 전이나 실패 시에는 출력값을 변경하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Stats")
	bool TryGetStats(FLSCastleStatRow& OutStats) const;

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

  protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class UFloatingPawnMovement> Movement;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Movement", meta = (ClampMin = "0"))
	float AcceptanceRadius = 20.0f;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<class ULSHealthComponent> Health;

	/** 수치 자체는 테이블에서 조정하고 여기서는 사용할 행만 지정한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Stats", meta = (RowType = "/Script/Landship.LSCastleStatRow"))
	FDataTableRowHandle StatsRow;

	/** 전투 대상 탐색에 사용하는 건물 충돌 영역이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Components")
	TObjectPtr<UBoxComponent> Collision;

  private:
	/** 실패 시 실행만 차단하며 사망 알림·랙돌·제거를 수행하지 않는다. */
	void NeutralizeCombat();
	void PrepareForDeath();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Combat",
	          meta = (ExposeOnSpawn = "true", AllowPrivateAccess = "true"))
	ELSFaction Faction = ELSFaction::Ally;

	bool bFactionLocked = false;

	/** 현재 전투 상태와 구분되는 초기 수치 복사본이다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	FLSCastleStatRow Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "Landship|Stats")
	ELSCombatStatsInitResult StatsInitResult = ELSCombatStatsInitResult::NotAttempted;
};
