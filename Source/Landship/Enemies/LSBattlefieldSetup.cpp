#include "Enemies/LSBattlefieldSetup.h"

#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Enemies/LSOutpost.h"
#include "EngineUtils.h"
#include "Misc/DataValidation.h"

ALSBattlefieldSetup::ALSBattlefieldSetup()
{
	PrimaryActorTick.bCanEverTick = false;
}

FString ALSBattlefieldSetup::GetConfigurationError() const
{
	if (!IsValid(PlayerCastle) || !IsValid(CoreOutpost))
	{
		return TEXT("PlayerCastle and CoreOutpost must both be assigned");
	}
	if (PlayerCastle->GetWorld() != GetWorld() || CoreOutpost->GetWorld() != GetWorld())
	{
		return TEXT("Battlefield references must belong to the setup world");
	}
	const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(PlayerCastle);
	const ULSSelectableComponent* Selection = PlayerCastle->FindComponentByClass<ULSSelectableComponent>();
	if (!Faction || Faction->GetFaction() != ELSFaction::Ally || !Selection ||
		Selection->Kind != ELSSelectionKind::Castle)
	{
		return TEXT("PlayerCastle must be an Ally with Castle selection kind");
	}
	TInlineComponentArray<ULSHealthComponent*> CastleHealth(PlayerCastle.Get());
	if (CastleHealth.Num() != 1)
	{
		return TEXT("PlayerCastle must have exactly one health component");
	}
	if (CoreOutpost->GetFaction() != ELSFaction::Enemy)
	{
		return TEXT("CoreOutpost must be Enemy");
	}
	TInlineComponentArray<ULSHealthComponent*> CoreHealth(CoreOutpost.Get());
	if (CoreHealth.Num() != 1 || CoreHealth[0]->bDestroyOnDeath)
	{
		return TEXT("CoreOutpost must have one health component and retain its actor on death");
	}
	return FString();
}

#if WITH_EDITOR
EDataValidationResult ALSBattlefieldSetup::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	if (GetWorld())
	{
		int32 SetupCount = 0;
		for (TActorIterator<ALSBattlefieldSetup> It(GetWorld()); It; ++It)
		{
			++SetupCount;
		}
		if (SetupCount != 1)
		{
			Context.AddError(FText::FromString(TEXT("A battlefield map must contain exactly one battlefield setup")));
			return EDataValidationResult::Invalid;
		}
	}
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty())
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
