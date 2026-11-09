#include "Combat/LSHealthComponent.h"

#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSFactionInterface.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DataValidation.h"
#include "HAL/ThreadSafeCounter64.h"
#include "TimerManager.h"

namespace
{
int64 NextLSDamageReceiverId()
{
	static FThreadSafeCounter64 NextReceiverId;
	return NextReceiverId.Increment();
}
}

ULSHealthComponent::ULSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	DamageReceiverId = NextLSDamageReceiverId();
}

bool ULSHealthComponent::TryGetLastDamage(FLSDamageResult& OutDamage) const
{
	if (!bHasTakenDamage)
		return false;
	OutDamage = LastDamage;
	return true;
}

bool ULSHealthComponent::ResetForPool()
{
	if (bProcessingDamage || !IsValid(GetOwner()) || !GetOwner()->IsHidden() || GetOwner()->GetActorEnableCollision())
		return false;
	OnDamagedEvent.Clear();
	OnDiedEvent.Clear();
	OnHealedEvent.Clear();
	PrepareDeathEvent.Unbind();
	ActiveHealer.Reset();
	HealingClaimSerial = 0;
	LastDamage = FLSDamageResult();
	bHasTakenDamage = false;
	DamageReceiverId = NextLSDamageReceiverId();
	Health = FLSHealthSnapshot();
	Armor = 0.0f;
	ArmorPenetration = 0.0f;
	InitializationState = ELSHealthInitializationState::NotAttempted;
	InitializationFailureReason.Reset();
	return true;
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
	const FLSPrepareCombatDeath& InPrepareDeathEvent, const FString& OwnerConfigurationError,
	float InitialArmor, float InitialArmorPenetration)
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
	if (!FMath::IsFinite(InitialArmor) || InitialArmor < 0.0f ||
	    !FMath::IsFinite(InitialArmorPenetration) || InitialArmorPenetration < 0.0f)
	{
		return FailInitialization(TEXT("Armor and armor penetration must be finite and >= 0"));
	}
	Armor = InitialArmor;
	ArmorPenetration = InitialArmorPenetration;
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

float ULSHealthComponent::RestoreHealth(float Amount)
{
	if (!IsValid(this) || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown ||
	    InitializationState != ELSHealthInitializationState::Ready || Health.bIsDead || bProcessingDamage ||
	    !FMath::IsFinite(Amount) || Amount <= 0)
		return 0;
	const float Applied = FMath::Min(Amount, Health.MaxHealth - Health.CurrentHealth);
	if (Applied <= 0)
		return 0;
	TGuardValue<bool> ChangeGuard(bProcessingDamage, true);
	Health.CurrentHealth += Applied;
	OnHealedEvent.Broadcast(Applied, Health.CurrentHealth);
	return Applied;
}

bool ULSHealthComponent::CanClaimHealing(const UActorComponent* Healer) const
{
	return IsValid(this) && IsValid(Healer) && IsValid(Healer->GetOwner()) &&
	    !Healer->GetOwner()->IsActorBeingDestroyed() && Healer->GetWorld() == GetWorld() &&
	    IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
	    GetWorld() && !GetWorld()->bIsTearingDown && InitializationState == ELSHealthInitializationState::Ready &&
	    !Health.bIsDead && (!ActiveHealer.IsValid() || ActiveHealer.Get() == Healer);
}

uint64 ULSHealthComponent::TryClaimHealing(UActorComponent* Healer)
{
	if (!CanClaimHealing(Healer))
		return 0;
	if (ActiveHealer.Get() == Healer && HealingClaimSerial != 0)
		return HealingClaimSerial;
	if (++NextHealingClaimSerial == 0)
		++NextHealingClaimSerial;
	ActiveHealer = Healer;
	HealingClaimSerial = NextHealingClaimSerial;
	return HealingClaimSerial;
}

bool ULSHealthComponent::HasHealingClaim(const UActorComponent* Healer, uint64 Serial) const
{
	return Serial != 0 && Serial == HealingClaimSerial && ActiveHealer.Get() == Healer && CanClaimHealing(Healer);
}

void ULSHealthComponent::ReleaseHealingClaim(const UActorComponent* Healer, uint64 Serial)
{
	// 환자가 풀에서 새 생명으로 재사용된 뒤의 과거 반환도 현재 치유 권한을 지우지 않는다.
	if (Serial != 0 && Serial == HealingClaimSerial && ActiveHealer.Get() == Healer)
	{
		ActiveHealer.Reset();
		HealingClaimSerial = 0;
	}
}

float ULSHealthComponent::RestoreClaimedHealth(UActorComponent* Healer, uint64 Serial, float Amount)
{
	return HasHealingClaim(Healer, Serial) ? RestoreHealth(Amount) : 0;
}

FLSDamageRequest ULSHealthComponent::MakeDamageRequest(float DamageAmount, AActor* SourceActor, AActor* DamageCauser)
{
	FLSDamageRequest Request;
	Request.DamageAmount = DamageAmount;
	Request.SourceActor = SourceActor;
	Request.DamageCauser = DamageCauser;
	if (IsValid(SourceActor))
	{
		if (const auto* SourceHealth = SourceActor->FindComponentByClass<ULSHealthComponent>())
		{
			Request.ArmorPenetration = SourceHealth->ArmorPenetration;
		}
		if (const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(SourceActor))
		{
			Request.bHasSourceFaction = true;
			Request.SourceFaction = Faction->GetFaction();
		}
	}
	return Request;
}

ELSApplyDamageStatus ULSHealthComponent::EvaluateDamagePolicy(const FLSDamageRequest& Request, const AActor* Receiver)
{
	const ULSCombatDeveloperSettings* Settings = GetDefault<ULSCombatDeveloperSettings>();
	const bool bSelfDamage = Request.SourceActor == Receiver;
	if (bSelfDamage && !Settings->bAllowSelfDamage)
	{
		return ELSApplyDamageStatus::RejectedSelfDamageDisabled;
	}
	if (!bSelfDamage && !Settings->bAllowFriendlyFire && Request.Policy == ELSDamagePolicy::Standard)
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
			return ELSApplyDamageStatus::RejectedFriendlyFireDisabled;
		}
	}
	return ELSApplyDamageStatus::Applied;
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
	TGuardValue<bool> DamageGuard(bProcessingDamage, true);
	if (InitializationState != ELSHealthInitializationState::Ready)
	{
		ensureAlwaysMsgf(false, TEXT("[LSHealthComponent::ApplyDamage] Health not ready: Owner=%s Reason=%s"),
			*GetPathNameSafe(GetOwner()), *InitializationFailureReason);
		return {InitializationState == ELSHealthInitializationState::Failed
			? ELSApplyDamageStatus::RejectedInitializationFailed : ELSApplyDamageStatus::RejectedNotReady, 0.0f};
	}
	if (Health.bIsDead)
	{
		// 거부 사유가 겹치면 생존 여부를 입력 검사보다 우선한다. 사망 대상은 요청을 더 처리하지 않는다.
		return {ELSApplyDamageStatus::RejectedDead, 0.0f};
	}
	if (!ensureAlwaysMsgf(FMath::IsFinite(Request.DamageAmount) && Request.DamageAmount >= 0.0f
		&& FMath::IsFinite(Request.ArmorPenetration) && Request.ArmorPenetration >= 0.0f
		&& (Request.Policy == ELSDamagePolicy::Standard || Request.Policy == ELSDamagePolicy::EnvironmentalHazard)
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
	const ELSApplyDamageStatus PolicyStatus = EvaluateDamagePolicy(Request, Receiver);
	if (PolicyStatus != ELSApplyDamageStatus::Applied)
	{
		return {PolicyStatus, 0.0f};
	}
	const float EffectiveArmor = Request.bIgnoreArmor ? 0.0f : FMath::Max(0.0f, Armor - Request.ArmorPenetration);
	const float DamageAfterArmor = FMath::Max(FMath::Min(1.0f, Request.DamageAmount), Request.DamageAmount - EffectiveArmor);
	const float AppliedDamage = FMath::Min(DamageAfterArmor, Health.CurrentHealth);
	const float PreviousHealth = Health.CurrentHealth;
	Health.CurrentHealth -= AppliedDamage;
	Health.bIsDead = Health.CurrentHealth == 0.0f;
	FLSDamageResult Damage;
	Damage.RequestedDamage = Request.DamageAmount;
	Damage.AppliedDamage = AppliedDamage;
	Damage.CurrentHealth = Health.CurrentHealth;
	Damage.PreviousHealth = PreviousHealth;
	Damage.MaxHealth = Health.MaxHealth;
	Damage.Receiver = Receiver;
	Damage.bReceiverHiddenAtCommit = Receiver->IsHidden();
	Damage.ReceiverId = DamageReceiverId;
	Damage.TimestampSeconds = GetWorld()->GetTimeSeconds();
	Damage.bHasSourceFaction = Request.bHasSourceFaction;
	Damage.SourceFaction = Request.SourceFaction;
	Damage.bHasAttackOrigin = Request.bHasAttackOrigin;
	Damage.AttackOrigin = Request.AttackOrigin;
	Damage.bHasImpactLocation = Request.bHasImpactLocation;
	Damage.ImpactLocation = Request.ImpactLocation;
	Damage.bHasIncomingDirection = Request.bHasIncomingDirection;
	Damage.IncomingDirection = Request.IncomingDirection;
	Damage.SourceActor = IsValid(Request.SourceActor) ? Request.SourceActor : nullptr;
	Damage.DamageCauser = IsValid(Request.DamageCauser) ? Request.DamageCauser : nullptr;
	if (!Damage.bHasSourceFaction && IsValid(Request.SourceActor))
	{
		if (const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Request.SourceActor))
		{
			Damage.bHasSourceFaction = true;
			Damage.SourceFaction = Faction->GetFaction();
		}
	}
	Damage.Source = Damage.SourceActor.Get();
	Damage.Causer = Damage.DamageCauser.Get();
	FLSDamageResult DeferredDamage = Damage;
	DeferredDamage.SourceActor = nullptr;
	DeferredDamage.DamageCauser = nullptr;
	LastDamage = DeferredDamage;
	bHasTakenDamage = true;
	// 액터에 묶인 타이머는 사망 훅에서 제거될 수 있다. 기록과 약한 구독을 값으로 복사한다.
	const FLSOnDamageCommitted CommittedListeners = OnDamageCommitted;
	const TWeakObjectPtr<UWorld> DamageWorld = GetWorld();
	const auto QueueCommitted = [DamageWorld, CommittedListeners, DeferredDamage]()
	{
		if (UWorld* World = DamageWorld.Get(); World && !World->bIsTearingDown)
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([CommittedListeners, DeferredDamage]()
			{
				CommittedListeners.Broadcast(DeferredDamage);
			}));
	};
	if (!Health.bIsDead)
	{
		QueueCommitted();
		OnDamagedEvent.Broadcast(Damage);
		return {ELSApplyDamageStatus::Applied, AppliedDamage};
	}

	// 상태 확정과 필수 처리가 끝난 대상만 공개 사망 알림에서 관찰할 수 있다.
	TWeakObjectPtr<AActor> WeakOwner(GetOwner());
	PrepareDeathEvent.Execute();
	QueueCommitted();
	AActor* Owner = WeakOwner.Get();
	// 필수 훅도 충돌 변경 콜백을 일으킨다. 그 안에서 끝난 수신자에게 공개 알림을 보내지 않는다.
	if (!IsValid(this) || !IsValid(Owner) || Owner->IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return {ELSApplyDamageStatus::Applied, AppliedDamage};
	}
	OnDiedEvent.Broadcast(Damage);
	Owner = WeakOwner.Get();
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
