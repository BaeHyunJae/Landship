#include "Castle/LSCastleWeaponData.h"
#include "Misc/DataValidation.h"

bool ULSCastleWeaponData::TryGetCannonArcSteps(const FLSCastleWeaponSettings& Settings, int32& OutSteps)
{
	if (!FMath::IsFinite(Settings.ArcHeight) || Settings.ArcHeight < 0 ||
	    !FMath::IsFinite(Settings.ShellRadius) || Settings.ShellRadius <= 0)
		return false;
	// 포물선과 현 사이의 최대 높이 차는 ArcHeight * 구간길이²다.
	const double Tolerance = FMath::Min(1.0, static_cast<double>(Settings.ShellRadius) * .1);
	const double RequiredSteps = FMath::Max(1.0, FMath::CeilToDouble(FMath::Sqrt(Settings.ArcHeight / Tolerance)));
	constexpr int32 MaxCannonArcSteps = 4096;
	if (!FMath::IsFinite(RequiredSteps) || RequiredSteps > MaxCannonArcSteps)
		return false;
	OutSteps = static_cast<int32>(RequiredSteps);
	return true;
}

bool ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon Weapon, const FLSCastleWeaponSettings& Settings)
{
	if (!FMath::IsFinite(Settings.Damage) || Settings.Damage < 0 || !FMath::IsFinite(Settings.Cooldown) ||
	    Settings.Cooldown <= 0 || !FMath::IsFinite(Settings.Range) || Settings.Range <= 0 ||
	    !FMath::IsFinite(Settings.ProjectileSpeed) || Settings.ProjectileSpeed <= 0)
		return false;
	if ((Weapon == ELSCastleWeapon::Cannon || Weapon == ELSCastleWeapon::Shotgun) &&
	    (!FMath::IsFinite(Settings.KnockbackSpeed) || Settings.KnockbackSpeed < 0 ||
	     !FMath::IsFinite(Settings.KnockbackLift) || Settings.KnockbackLift < 0))
		return false;
	if (Weapon == ELSCastleWeapon::Shotgun)
	{
		return FMath::IsFinite(Settings.ConeAngle) && Settings.ConeAngle > 0 && Settings.ConeAngle <= 180 &&
		       Settings.PelletCount >= 1 && Settings.PelletCount <= 64 && FMath::IsFinite(Settings.PelletRadius) &&
		       Settings.PelletRadius > 0;
	}
	if (!FMath::IsFinite(Settings.Radius) || Settings.Radius <= 0)
		return false;
	int32 ArcSteps;
	return Weapon != ELSCastleWeapon::Cannon || TryGetCannonArcSteps(Settings, ArcSteps);
}

const FLSCastleWeaponSettings* ULSCastleWeaponData::FindLevel(int32 Level) const
{
	if (Level < 1 || !Levels.IsValidIndex(Level - 1) || !AreSettingsValid(Weapon, Levels[Level - 1]))
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
		if (!AreSettingsValid(Weapon, Settings))
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
