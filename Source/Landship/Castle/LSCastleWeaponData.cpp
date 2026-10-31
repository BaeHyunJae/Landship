#include "Castle/LSCastleWeaponData.h"
#include "Misc/DataValidation.h"

bool ULSCastleWeaponData::IsLevelValid(const FLSCastleWeaponSettings& Settings) const
{
	if (!FMath::IsFinite(Settings.Damage) || Settings.Damage < 0 || !FMath::IsFinite(Settings.Cooldown) ||
	    Settings.Cooldown <= 0 || !FMath::IsFinite(Settings.Range) || Settings.Range <= 0)
		return false;
	if (Weapon == ELSCastleWeapon::Shotgun)
	{
		return FMath::IsFinite(Settings.ConeAngle) && Settings.ConeAngle > 0 && Settings.ConeAngle <= 180 &&
		       FMath::IsFinite(Settings.KnockbackSpeed) && Settings.KnockbackSpeed >= 0 &&
		       FMath::IsFinite(Settings.KnockbackLift) && Settings.KnockbackLift >= 0;
	}
	return FMath::IsFinite(Settings.Radius) && Settings.Radius > 0 &&
	       (Weapon != ELSCastleWeapon::Ballista ||
	        (FMath::IsFinite(Settings.ProjectileSpeed) && Settings.ProjectileSpeed > 0));
}

const FLSCastleWeaponSettings* ULSCastleWeaponData::FindLevel(int32 Level) const
{
	if (Level < 1 || !Levels.IsValidIndex(Level - 1) || !IsLevelValid(Levels[Level - 1]))
		return nullptr;
	return &Levels[Level - 1];
}

bool ULSCastleWeaponData::IsDefinitionValid() const
{
	if (Weapon != ELSCastleWeapon::Cannon && Weapon != ELSCastleWeapon::Shotgun && Weapon != ELSCastleWeapon::Ballista)
		return false;
	if (Levels.IsEmpty())
		return false;
	for (const FLSCastleWeaponSettings& Settings : Levels)
	{
		if (!IsLevelValid(Settings))
			return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult ULSCastleWeaponData::IsDataValid(FDataValidationContext& Context) const
{
	if (!IsDefinitionValid())
	{
		Context.AddError(FText::FromString(TEXT("무장 ID 또는 레벨별 수치가 유효하지 않습니다.")));
		return EDataValidationResult::Invalid;
	}
	return Super::IsDataValid(Context) == EDataValidationResult::Invalid ? EDataValidationResult::Invalid
	                                                                     : EDataValidationResult::Valid;
}
#endif
