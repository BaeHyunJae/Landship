#pragma once

#include "Components/ActorComponent.h"
#include "Castle/LSCastleWeaponData.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSCastleWeaponComponent.generated.h"

class ALSPathObstacle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSWeaponStateChanged, ELSCastleWeapon, Weapon,
                                             const FLSCastleWeaponState&, State);

/** 성의 수동 원형·부채꼴·직선 무장. 선택과 조준 입력은 컨트롤러가 담당한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCastleWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSCastleWeaponComponent();
	/** 이미 날아간 투사체를 유지하고 이후 무장 사용만 영구 중단한다. */
	void EndBattleActions();
	/** 생성 단계의 데이터 지정. 플레이 시작 후에는 변경하지 않는다. 실패 시 기존 구성을 유지한다. */
	bool SetWeaponDefinitions(const TArray<ULSCastleWeaponData*>& Definitions);
	UFUNCTION(BlueprintPure, Category = "Landship|Weapons")
	bool GetWeaponState(ELSCastleWeapon Weapon, FLSCastleWeaponState& OutState) const;
	UFUNCTION(BlueprintPure, Category = "Landship|Weapons")
	bool GetResolvedSettings(ELSCastleWeapon Weapon, FLSCastleWeaponSettings& OutSettings) const;
	UFUNCTION(BlueprintPure, Category = "Landship|Weapons")
	int32 GetMaxLevel(ELSCastleWeapon Weapon) const;
	UFUNCTION(BlueprintCallable, Category = "Landship|Weapons")
	ELSWeaponChangeResult UnlockWeapon(ELSCastleWeapon Weapon);
	UFUNCTION(BlueprintCallable, Category = "Landship|Weapons")
	ELSWeaponChangeResult SetWeaponUnlocked(ELSCastleWeapon Weapon, bool bUnlocked);
	UFUNCTION(BlueprintCallable, Category = "Landship|Weapons")
	ELSWeaponChangeResult SetWeaponEnabled(ELSCastleWeapon Weapon, bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Landship|Weapons")
	ELSWeaponChangeResult UpgradeWeapon(ELSCastleWeapon Weapon);
	UPROPERTY(BlueprintAssignable, Category = "Landship|Weapons")
	FLSWeaponStateChanged OnWeaponStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Landship|Weapons")
	FLSAttackExecuted OnAttackExecutedEvent;
	const FLSCastleWeaponSettings* FindSettings(ELSCastleWeapon Weapon) const;
	float GetRemainingCooldown(ELSCastleWeapon Weapon) const;
	/** 현재 쿨타임을 시작할 때 확정한 길이. 강화로 진행 막대가 바뀌지 않는다. */
	float GetCooldownDuration(ELSCastleWeapon Weapon) const;
	ELSWeaponCastResult GetStatus(ELSCastleWeapon Weapon) const;
	ELSWeaponCastResult ValidateAim(ELSCastleWeapon Weapon, const FVector& Aim) const;
	ELSWeaponCastResult CastWeapon(ELSCastleWeapon Weapon, const FVector& Aim);
	bool GetObstacleAimPoint(ALSPathObstacle* Target, FVector& OutAim) const;
	ELSWeaponCastResult ValidateObstacleAim(ELSCastleWeapon Weapon, ALSPathObstacle* Target) const;
	ELSWeaponCastResult CastWeaponAtObstacle(ELSCastleWeapon Weapon, ALSPathObstacle* Target);
	bool GetRecentCast(ELSCastleWeapon& Weapon, FVector& Aim, FLSCastleWeaponSettings* Settings = nullptr) const;
	/** 무장별 로컬 오프셋의 기준 위치다. 조준 방향을 반영한 실제 발사점은 GetWeaponFireOrigin을 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Weapons")
	FVector GetWeaponLaunchLocation(ELSCastleWeapon Weapon) const;
	/** 산탄은 조준 방향의 실제 성 충돌 외곽에서 시작하고, 나머지 무장은 기존 기준 위치를 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Weapons")
	FVector GetWeaponFireOrigin(ELSCastleWeapon Weapon, const FVector& Aim) const;
	/** 성 중심에서 대포 발사 위치까지의 로컬 오프셋(cm). 액터 크기 배율은 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Weapons")
	FVector CannonLaunchOffset = FVector(0, 0, 150);
	/** 산탄의 발사 평면 높이를 정하는 로컬 오프셋(cm). 실제 XY 발사점은 조준 방향의 성 외곽이다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Weapons")
	FVector ShotgunLaunchOffset = FVector::ZeroVector;
	/** 성 중심에서 발리스타 발사 위치까지의 로컬 오프셋(cm). 지면 조준 탄은 이 높이에서 수평으로 날아간다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Weapons")
	FVector BallistaLaunchOffset = FVector::ZeroVector;

  protected:
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, Category = "Landship|Weapons")
	TArray<TSoftObjectPtr<ULSCastleWeaponData>> WeaponDefinitions;
	UPROPERTY(EditAnywhere, Category = "Landship|Weapons")
	TMap<ELSCastleWeapon, FLSCastleWeaponState> InitialStates;

  private:
	ELSWeaponCastResult CastWeaponInternal(ELSCastleWeapon Weapon, const FVector& Aim, bool bFirstBlocking);
	ELSWeaponChangeResult ValidateChange(ELSCastleWeapon Weapon) const;
	void NotifyStateChanged(ELSCastleWeapon Weapon);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Landship|Weapons",
	          meta = (AllowPrivateAccess = "true"))
	TMap<ELSCastleWeapon, FLSCastleWeaponState> States;
	TMap<ELSCastleWeapon, float> CooldownDurations;
	UPROPERTY(Transient)
	TMap<ELSCastleWeapon, TObjectPtr<ULSCastleWeaponData>> Definitions;
	FLSCastleWeaponSettings LastCastSettings;
	ELSCastleWeapon LastWeapon = ELSCastleWeapon::Cannon;
	FVector LastAim = FVector::ZeroVector;
	double LastCastTime = -100;
	bool bCasting = false;
	bool bChangingState = false;
	bool bBattleActionsEnded = false;
	int64 LastExecutionId = 0;
};
