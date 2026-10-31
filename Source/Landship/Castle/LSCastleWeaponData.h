#pragma once

#include "Engine/DataAsset.h"
#include "LSCastleWeaponData.generated.h"

UENUM(BlueprintType)
enum class ELSCastleWeapon : uint8
{
	Cannon,
	Shotgun,
	Ballista
};

UENUM(BlueprintType)
enum class ELSWeaponCastResult : uint8
{
	Ready,
	Cooldown,
	OutOfRange,
	InvalidAim,
	Unavailable,
	Busy,
	Locked,
	Disabled,
	InvalidData
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCastleWeaponSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	float Damage = 60;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0.01", Units = "s"))
	float Cooldown = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1", Units = "cm"))
	float Range = 1200;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1", Units = "cm"))
	float Radius = 250;
	/** 산탄의 전체 부채꼴 각도다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1", ClampMax = "180"))
	float ConeAngle = 70;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0", Units = "cm/s"))
	float KnockbackSpeed = 650;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0", Units = "cm/s"))
	float KnockbackLift = 180;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1", Units = "cm/s"))
	float ProjectileSpeed = 1800;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCastleWeaponState
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bUnlocked = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1"))
	int32 Level = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	double ReadyTime = 0;
};

UENUM(BlueprintType)
enum class ELSWeaponChangeResult : uint8
{
	Success,
	InvalidWeapon,
	InvalidData,
	Locked,
	MaxLevel,
	Unavailable,
	Busy
};

/** 무장 하나의 레벨별 최종 수치. 배열의 첫 항목이 1레벨이다. */
UCLASS(BlueprintType)
class LANDSHIP_API ULSCastleWeaponData : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ELSCastleWeapon Weapon = ELSCastleWeapon::Cannon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (TitleProperty = "Damage"))
	TArray<FLSCastleWeaponSettings> Levels;
	const FLSCastleWeaponSettings* FindLevel(int32 Level) const;
	bool IsDefinitionValid() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
  private:
	bool IsLevelValid(const FLSCastleWeaponSettings& Settings) const;
};
