#pragma once

#include "Components/ActorComponent.h"
#include "Castle/LSCastleWeaponData.h"
#include "LSCastleWeaponComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLSWeaponStateChanged, ELSCastleWeapon, Weapon,
                                             const FLSCastleWeaponState&, State);

/** 성의 수동 원형·부채꼴·직선 무장. 선택과 조준 입력은 컨트롤러가 담당한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCastleWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSCastleWeaponComponent();
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
	const FLSCastleWeaponSettings* FindSettings(ELSCastleWeapon Weapon) const;
	float GetRemainingCooldown(ELSCastleWeapon Weapon) const;
	ELSWeaponCastResult GetStatus(ELSCastleWeapon Weapon) const;
	ELSWeaponCastResult ValidateAim(ELSCastleWeapon Weapon, const FVector& Aim) const;
	ELSWeaponCastResult CastWeapon(ELSCastleWeapon Weapon, const FVector& Aim);
	bool GetRecentCast(ELSCastleWeapon& Weapon, FVector& Aim, FLSCastleWeaponSettings* Settings = nullptr) const;

  protected:
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, Category = "Landship|Weapons")
	TArray<TSoftObjectPtr<ULSCastleWeaponData>> WeaponDefinitions;
	UPROPERTY(EditAnywhere, Category = "Landship|Weapons")
	TMap<ELSCastleWeapon, FLSCastleWeaponState> InitialStates;

  private:
	ELSWeaponChangeResult ValidateChange(ELSCastleWeapon Weapon) const;
	void NotifyStateChanged(ELSCastleWeapon Weapon);
	void ApplyAreaDamage(ELSCastleWeapon Weapon, const FVector& Aim, const FLSCastleWeaponSettings& Settings);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Landship|Weapons",
	          meta = (AllowPrivateAccess = "true"))
	TMap<ELSCastleWeapon, FLSCastleWeaponState> States;
	UPROPERTY(Transient)
	TMap<ELSCastleWeapon, TObjectPtr<ULSCastleWeaponData>> Definitions;
	FLSCastleWeaponSettings LastCastSettings;
	ELSCastleWeapon LastWeapon = ELSCastleWeapon::Cannon;
	FVector LastAim = FVector::ZeroVector;
	double LastCastTime = -100;
	bool bCasting = false;
	bool bChangingState = false;
};
