#include "LSCombatStats.h"

DEFINE_LOG_CATEGORY(LogLSCombatStats);

namespace
{
	bool IsNonnegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f;
	}

	bool IsPositiveFinite(float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}
}

const TCHAR* LSCombatStats::Validate(const FLSUnitStatRow& Stats)
{
	if (!IsPositiveFinite(Stats.Health))
	{
		return TEXT("InvalidValue: Health must be finite and > 0");
	}
	if (!IsNonnegativeFinite(Stats.AttackPower))
	{
		return TEXT("InvalidValue: AttackPower must be finite and >= 0");
	}
	if (!IsPositiveFinite(Stats.AttackInterval))
	{
		return TEXT("InvalidValue: AttackInterval must be finite and > 0");
	}
	if (!IsNonnegativeFinite(Stats.AttackRange))
	{
		return TEXT("InvalidValue: AttackRange must be finite and >= 0");
	}
	if (!IsNonnegativeFinite(Stats.DetectionRange))
	{
		return TEXT("InvalidValue: DetectionRange must be finite and >= 0");
	}
	if (!IsNonnegativeFinite(Stats.VisionRadius))
	{
		return TEXT("InvalidValue: VisionRadius must be finite and >= 0");
	}
	if (!IsNonnegativeFinite(Stats.MovementSpeed))
	{
		return TEXT("InvalidValue: MovementSpeed must be finite and >= 0");
	}
	return nullptr;
}

const TCHAR* LSCombatStats::Validate(const FLSCastleStatRow& Stats)
{
	if (!IsPositiveFinite(Stats.Health))
	{
		return TEXT("InvalidValue: Health must be finite and > 0");
	}
	if (!IsNonnegativeFinite(Stats.VisionRadius))
	{
		return TEXT("InvalidValue: VisionRadius must be finite and >= 0");
	}
	if (!IsNonnegativeFinite(Stats.MovementSpeed))
	{
		return TEXT("InvalidValue: MovementSpeed must be finite and >= 0");
	}
	return nullptr;
}

const TCHAR* LSCombatStats::Validate(const FLSOutpostStatRow& Stats)
{
	if (!IsPositiveFinite(Stats.Health))
	{
		return TEXT("InvalidValue: Health must be finite and > 0");
	}
	if (!IsNonnegativeFinite(Stats.AttackPower))
	{
		return TEXT("InvalidValue: AttackPower must be finite and >= 0");
	}
	if (!IsPositiveFinite(Stats.AttackInterval))
	{
		return TEXT("InvalidValue: AttackInterval must be finite and > 0");
	}
	if (!IsNonnegativeFinite(Stats.AttackRange))
	{
		return TEXT("InvalidValue: AttackRange must be finite and >= 0");
	}
	return nullptr;
}

ELSCombatStatsInitResult LSCombatStats::ReportFailure(const FDataTableRowHandle& Handle,
	const UObject* Owner, ELSCombatStatsInitResult Result, const TCHAR* Reason)
{
	ensureAlwaysMsgf(false, TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed: Owner=%s Table=%s Row=%s Reason=%s"),
		*GetPathNameSafe(Owner), *GetPathNameSafe(Handle.DataTable), *Handle.RowName.ToString(), Reason);
	return Result;
}
