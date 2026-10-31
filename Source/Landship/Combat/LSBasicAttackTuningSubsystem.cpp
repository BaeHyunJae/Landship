#include "Combat/LSBasicAttackTuningSubsystem.h"

#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCombatStats.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"

bool ULSBasicAttackTuningSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULSBasicAttackTuningSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const float ConfiguredMultiplier = GetDefault<ULSCombatDeveloperSettings>()->BasicAttackSpeedMultiplier;
	if (!FMath::IsFinite(ConfiguredMultiplier) || ConfiguredMultiplier <= 0.0f)
	{
		UE_LOGFMT(LogLSCombatStats, Warning,
		          "[{Function}] Invalid BasicAttackSpeedMultiplier={Multiplier}: World={World}. "
		          "A finite positive value is required; multiplier queries will fail for this play.",
		          ("Function", __FUNCTION__), ("Multiplier", ConfiguredMultiplier), ("World", InWorld.GetPathName()));
		return;
	}

	BasicAttackSpeedMultiplier = ConfiguredMultiplier;
	bHasValidMultiplier = true;
	UE_LOGFMT(LogLSCombatStats, Display,
	          "[{Function}] BasicAttackSpeedMultiplier={Multiplier} captured for World={World}",
	          ("Function", __FUNCTION__), ("Multiplier", BasicAttackSpeedMultiplier), ("World", InWorld.GetPathName()));
}

bool ULSBasicAttackTuningSubsystem::TryGetBasicAttackSpeedMultiplier(float& OutMultiplier) const
{
	if (!bHasValidMultiplier)
	{
		return false;
	}

	OutMultiplier = BasicAttackSpeedMultiplier;
	return true;
}

void ULSBasicAttackTuningSubsystem::Deinitialize()
{
	bHasValidMultiplier = false;
	Super::Deinitialize();
}
