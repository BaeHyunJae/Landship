#include "Combat/LSHealthComponent.h"

#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSFactionInterface.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DataValidation.h"

ULSHealthComponent::ULSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FString ULSHealthComponent::GetConfigurationError() const
{
	return FMath::IsFinite(DestroyDelay) && DestroyDelay >= 0.0f
		? FString() : TEXT("DestroyDelay must be finite and >= 0");
}

bool ULSHealthComponent::FailInitialization(const FString& Reason)
{
	InitializationState = ELSHealthInitializationState::Failed;
	InitializationFailureReason = Reason;
	ensureAlwaysMsgf(false, TEXT("[LSHealthComponent::InitializeHealth] Owner=%s Reason=%s"),
		*GetPathNameSafe(GetOwner()), *Reason);
	return false;
}

bool ULSHealthComponent::InitializeHealth(ELSCombatStatsInitResult StatsResult, float InitialHealth,
	const FLSPrepareCombatDeath& InPrepareDeathEvent, const FString& OwnerConfigurationError)
{
	if (!ensureAlwaysMsgf(InitializationState == ELSHealthInitializationState::NotAttempted,
		TEXT("[LSHealthComponent::InitializeHealth] Reinitialization rejected: Owner=%s"), *GetPathNameSafe(GetOwner())))
	{
		return false;
	}
	if (StatsResult != ELSCombatStatsInitResult::Success)
	{
		// 실제 수치 읽기의 ensure 진단과 실패 원인은 소유 대상이 이미 보존했다.
		InitializationState = ELSHealthInitializationState::Failed;
		InitializationFailureReason = TEXT("Combat stats initialization failed");
		return false;
	}
	if (!OwnerConfigurationError.IsEmpty())
	{
		return FailInitialization(OwnerConfigurationError);
	}
	const FString ConfigurationError = GetConfigurationError();
	if (!ConfigurationError.IsEmpty())
	{
		return FailInitialization(ConfigurationError);
	}
	if (!IsValid(GetOwner()))
	{
		return FailInitialization(TEXT("Missing owner"));
	}
	TInlineComponentArray<ULSHealthComponent*> Components(GetOwner());
	if (Components.Num() != 1 || Components[0] != this)
	{
		return FailInitialization(TEXT("Exactly one health component is required"));
	}
	if (!InPrepareDeathEvent.IsBound())
	{
		return FailInitialization(TEXT("Missing native death handler"));
	}
	if (!FMath::IsFinite(InitialHealth) || InitialHealth <= 0.0f)
	{
		return FailInitialization(TEXT("Initial health must be finite and > 0"));
	}
	PrepareDeathEvent = InPrepareDeathEvent;
	Health.MaxHealth = InitialHealth;
	Health.CurrentHealth = InitialHealth;
	Health.bIsDead = false;
	InitializationState = ELSHealthInitializationState::Ready;
	return true;
}

bool ULSHealthComponent::TryGetHealth(FLSHealthSnapshot& OutHealth) const
{
	if (InitializationState != ELSHealthInitializationState::Ready)
	{
		return false;
	}
	OutHealth = Health;
	return true;
}

FLSDamageRequest ULSHealthComponent::MakeDamageRequest(float DamageAmount, AActor* SourceActor, AActor* DamageCauser)
{
	FLSDamageRequest Request;
	Request.DamageAmount = DamageAmount;
	Request.SourceActor = SourceActor;
	Request.DamageCauser = DamageCauser;
	if (IsValid(SourceActor))
	{
		if (const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(SourceActor))
		{
			Request.bHasSourceFaction = true;
			Request.SourceFaction = Faction->GetFaction();
		}
	}
	return Request;
}

FLSApplyDamageResult ULSHealthComponent::ApplyDamage(const FLSDamageRequest& Request)
{
	AActor* Receiver = GetOwner();
	if (!IsValid(this) || !IsValid(Receiver) || Receiver->IsActorBeingDestroyed()
		|| !GetWorld() || GetWorld()->bIsTearingDown)
	{
		// 공개 알림 중 제거된 수신자에게 재진입한 요청은 상태와 필수 훅에 접근하지 않는다.
		return {ELSApplyDamageStatus::RejectedInvalidTarget, 0.0f};
	}
	if (InitializationState != ELSHealthInitializationState::Ready)
	{
		ensureAlwaysMsgf(false, TEXT("[LSHealthComponent::ApplyDamage] Health not ready: Owner=%s Reason=%s"),
			*GetPathNameSafe(GetOwner()), *InitializationFailureReason);
		return {InitializationState == ELSHealthInitializationState::Failed
			? ELSApplyDamageStatus::RejectedInitializationFailed : ELSApplyDamageStatus::RejectedNotReady, 0.0f};
	}
	if (Health.bIsDead)
	{
		return {ELSApplyDamageStatus::RejectedDead, 0.0f};
	}
	if (!ensureAlwaysMsgf(FMath::IsFinite(Request.DamageAmount) && Request.DamageAmount >= 0.0f
		&& (!Request.bHasSourceFaction || LSFaction::IsValid(Request.SourceFaction)),
		TEXT("[LSHealthComponent::ApplyDamage] Invalid damage: Owner=%s Amount=%g HasSourceFaction=%d SourceFaction=%d"),
		*GetPathNameSafe(GetOwner()), Request.DamageAmount, !!Request.bHasSourceFaction, static_cast<int32>(Request.SourceFaction)))
	{
		return {ELSApplyDamageStatus::RejectedInvalidInput, 0.0f};
	}
	if (Request.DamageAmount == 0.0f)
	{
		return {ELSApplyDamageStatus::NoEffectZeroDamage, 0.0f};
	}
	const ULSCombatDeveloperSettings* Settings = GetDefault<ULSCombatDeveloperSettings>();
	const bool bSelfDamage = Request.SourceActor == Receiver;
	if (bSelfDamage && !Settings->bAllowSelfDamage)
	{
		return {ELSApplyDamageStatus::RejectedSelfDamageDisabled, 0.0f};
	}
	if (!bSelfDamage && !Settings->bAllowFriendlyFire)
	{
		bool bHasSourceFaction = Request.bHasSourceFaction;
		ELSFaction SourceFaction = Request.SourceFaction;
		if (!bHasSourceFaction && IsValid(Request.SourceActor))
		{
			if (const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Request.SourceActor))
			{
				bHasSourceFaction = true;
				SourceFaction = Faction->GetFaction();
			}
		}
		const ILSFactionInterface* ReceiverFaction = Cast<ILSFactionInterface>(Receiver);
		if (bHasSourceFaction && ReceiverFaction && SourceFaction == ReceiverFaction->GetFaction())
		{
			return {ELSApplyDamageStatus::RejectedFriendlyFireDisabled, 0.0f};
		}
	}
	const float AppliedDamage = FMath::Min(Request.DamageAmount, Health.CurrentHealth);
	Health.CurrentHealth -= AppliedDamage;
	Health.bIsDead = Health.CurrentHealth == 0.0f;
	FLSDamageResult Damage;
	Damage.RequestedDamage = Request.DamageAmount;
	Damage.AppliedDamage = AppliedDamage;
	Damage.CurrentHealth = Health.CurrentHealth;
	Damage.SourceActor = IsValid(Request.SourceActor) ? Request.SourceActor : nullptr;
	Damage.DamageCauser = IsValid(Request.DamageCauser) ? Request.DamageCauser : nullptr;
	if (!Health.bIsDead)
	{
		OnDamagedEvent.Broadcast(Damage);
		return {ELSApplyDamageStatus::Applied, AppliedDamage};
	}

	// 상태 확정과 필수 처리가 끝난 대상만 공개 사망 알림에서 관찰할 수 있다.
	PrepareDeathEvent.Execute();
	TWeakObjectPtr<AActor> WeakOwner(GetOwner());
	OnDiedEvent.Broadcast(Damage);
	AActor* Owner = WeakOwner.Get();
	if (IsValid(this) && IsValid(Owner) && !Owner->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown && bDestroyOnDeath)
	{
		if (DestroyDelay == 0.0f)
		{
			Owner->Destroy();
		}
		else
		{
			Owner->SetLifeSpan(DestroyDelay);
		}
	}
	return {ELSApplyDamageStatus::Applied, AppliedDamage};
}

#if WITH_EDITOR
EDataValidationResult ULSHealthComponent::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult BaseResult = Super::IsDataValid(Context);
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty())
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return BaseResult == EDataValidationResult::Invalid ? BaseResult : EDataValidationResult::Valid;
}
#endif
