#include "Enemies/LSBattlefieldSetup.h"

#include "Castle/LSCastle.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Enemies/LSOutpost.h"
#include "EngineUtils.h"
#include "Misc/DataValidation.h"
#if WITH_EDITOR
#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionParticipantComponent.h"
#endif

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
	if (!PlayerCastle->IsA<ALSCastle>())
	{
		return TEXT("PlayerCastle must be an actual ALSCastle or derived class; movement-test MobileCastle is not supported");
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
		// 미개방 맵의 레벨은 비활성이므로 데이터 검증에서 활성 레벨 필터를 쓰지 않는다.
		for (TActorIterator<ALSBattlefieldSetup> It(GetWorld(), StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
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
	const bool bHasBattlefieldSettings = GetWorld() && TActorIterator<ALSBattlefieldSettings>(
		GetWorld(), ALSBattlefieldSettings::StaticClass(), EActorIteratorFlags::SkipPendingKill);
	if (bHasBattlefieldSettings)
	{
		bool bInvalidCoreMarker = false;
		const ULSVisionParticipantComponent* CoreVision = CoreOutpost->FindComponentByClass<ULSVisionParticipantComponent>();
		if (!CoreVision || !CoreVision->bPublicCorePosition)
		{
			Context.AddError(FText::FromString(TEXT("CoreOutpost must have a vision participant with bPublicCorePosition enabled")));
			bInvalidCoreMarker = true;
		}
		for (TActorIterator<AActor> It(GetWorld(), AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
		{
			if (*It == CoreOutpost.Get())
			{
				continue;
			}
			TInlineComponentArray<ULSVisionParticipantComponent*> Participants(*It);
			for (const ULSVisionParticipantComponent* Participant : Participants)
			{
				if (Participant->bPublicCorePosition)
				{
					Context.AddError(FText::FromString(FString::Printf(
						TEXT("Only CoreOutpost may enable bPublicCorePosition; found on %s"), *It->GetPathName())));
					bInvalidCoreMarker = true;
				}
			}
		}
		if (bInvalidCoreMarker)
		{
			return EDataValidationResult::Invalid;
		}
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
