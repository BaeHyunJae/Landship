#include "Troops/LSArcherComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
const FName SuppressionSource(TEXT("ArcherHitSuppression"));
}

bool ULSArcherComponent::IsConfigurationValid() const
{
	return Super::IsConfigurationValid() && GetTroop()->GetTroopType() == ELSTroopType::Archer &&
	       FMath::IsFinite(SuppressionDuration) && SuppressionDuration > 0 &&
	       FMath::IsFinite(SuppressedIntervalMultiplier) && SuppressedIntervalMultiplier > 1;
}

void ULSArcherComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsConfigurationValid())
	{
		UE_LOG(LogTemp, Error, TEXT("%s: 궁병 피격 디버프 설정이 유효하지 않습니다."), *GetPathName());
		return;
	}
	if (ULSHealthComponent* Health = GetTroop()->FindComponentByClass<ULSHealthComponent>())
	{
		Health->OnDamagedEvent.AddUniqueDynamic(this, &ULSArcherComponent::OnDamaged);
		Health->OnDiedEvent.AddUniqueDynamic(this, &ULSArcherComponent::OnDied);
	}
}

void ULSArcherComponent::OnDamaged(const FLSDamageResult& Damage)
{
	ALSTroopUnit* Unit = GetTroop();
	if (!FMath::IsFinite(Damage.AppliedDamage) || Damage.AppliedDamage <= 0 ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Unit) || !IsConfigurationValid())
	{
		return;
	}
	if (!Unit->BasicAttack->SetAttackIntervalMultiplier(SuppressionSource, SuppressedIntervalMultiplier))
	{
		return;
	}
	bSuppressed = true;
	// 연속 피격은 만료 시각만 갱신한다. 이미 비행 중인 화살과 공격 표적은 건드리지 않는다.
	GetWorld()->GetTimerManager().SetTimer(SuppressionTimer, this, &ULSArcherComponent::ClearSuppression,
	                                       SuppressionDuration, false);
	if (!IsStaggered())
	{
		Unit->SetTacticalLabel(GetRestingLabel());
	}
}

void ULSArcherComponent::ClearSuppression()
{
	bSuppressed = false;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SuppressionTimer);
	}
	if (ALSTroopUnit* Unit = GetTroop())
	{
		Unit->BasicAttack->SetAttackIntervalMultiplier(SuppressionSource, 1);
		if (!IsStaggered())
		{
			Unit->SetTacticalLabel(GetRestingLabel());
		}
	}
}

void ULSArcherComponent::OnDied(const FLSDamageResult& Damage)
{
	ClearSuppression();
}

void ULSArcherComponent::SetBattleEnabled(bool bEnabled)
{
	Super::SetBattleEnabled(bEnabled);
	if (!IsBattleEnabled())
	{
		ClearSuppression();
	}
}

FString ULSArcherComponent::GetRestingLabel() const
{
	return bSuppressed ? TEXT("SUPPRESSED") : FString();
}

void ULSArcherComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ALSTroopUnit* Unit = GetTroop())
	{
		if (ULSHealthComponent* Health = Unit->FindComponentByClass<ULSHealthComponent>())
		{
			Health->OnDamagedEvent.RemoveDynamic(this, &ULSArcherComponent::OnDamaged);
			Health->OnDiedEvent.RemoveDynamic(this, &ULSArcherComponent::OnDied);
		}
	}
	ClearSuppression();
	Super::EndPlay(Reason);
}
