#include "Enemies/LSOutpost.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "AIController.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/DataValidation.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"

ALSOutpost::ALSOutpost()
{
	PrimaryActorTick.bCanEverTick = false;
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Selection->Kind = ELSSelectionKind::Building;
	Orders = CreateDefaultSubobject<ULSOrderComponent>(TEXT("Orders"));
	Vision = CreateDefaultSubobject<ULSVisionParticipantComponent>(TEXT("Vision"));
	Vision->MarkerKind = ELSMinimapMarkerKind::Building;
	Vision->bTriggersEnemyDiscovery = true;
	BasicAttack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("BasicAttack"));
	BasicAttack->SetAutoAttackOnBeginPlay(false);
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
}

void ALSOutpost::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	bFactionLocked = true;
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (!Health->InitializeHealth(StatsInitResult, Stats.Health,
		FLSPrepareCombatDeath::CreateUObject(this, &ALSOutpost::PrepareForDeath), GetCombatConfigurationError()))
	{
		NeutralizeCombat();
	}
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} stats initialized: Health={Health} AttackPower={AttackPower} AttackInterval={AttackInterval} AttackRange={AttackRange}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("AttackPower", Stats.AttackPower),
			("AttackInterval", Stats.AttackInterval),
			("AttackRange", Stats.AttackRange));
	}
	Selection->SetPlayerControlled(GetFaction() == ELSFaction::Ally);
	if (Health->GetInitializationState() == ELSHealthInitializationState::Ready)
	{
		FLSBasicAttackSettings AttackSettings;
		AttackSettings.Damage = Stats.AttackPower;
		AttackSettings.Interval = Stats.AttackInterval;
		AttackSettings.Range = Stats.AttackRange;
		BasicAttack->ConfigureAttack(AttackSettings);
	}
	if (!bArmed)
	{
		BasicAttack->Deactivate();
	}
	Super::BeginPlay();
}

bool ALSOutpost::TryGetStats(FLSOutpostStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}


void ALSOutpost::SetInitialFaction(ELSFaction InFaction)
{
	if (!ensureAlwaysMsgf(!bFactionLocked, TEXT("[LSOutpost::SetInitialFaction] Faction is locked: Owner=%s"), *GetPathName()))
	{
		return;
	}
	Faction = InFaction;
}

FString ALSOutpost::GetCombatConfigurationError() const
{
	if (!LSFaction::IsValid(Faction))
	{
		return TEXT("Invalid faction");
	}
	if (!FMath::IsFinite(DefenseRadius) || DefenseRadius < 0)
	{
		return TEXT("DefenseRadius must be finite and nonnegative");
	}
	TInlineComponentArray<ULSHealthComponent*> HealthComponents(this);
	if (HealthComponents.Num() != 1 || HealthComponents[0] != Health)
	{
		return TEXT("Exactly one owned health subobject is required");
	}
	return FString();
}

void ALSOutpost::NeutralizeCombat()
{
	BasicAttack->StopAttack();

	const auto CanContinue = [this]()
	{
		return IsValid(this) && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown;
	};
	if (!CanContinue())
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!CanContinue())
		{
			return;
		}
		if (!IsValid(Primitive))
		{
			continue;
		}
		Primitive->SetCanEverAffectNavigation(false);
		if (!CanContinue())
		{
			return;
		}
		if (!IsValid(Primitive))
		{
			continue;
		}
		// 충돌 변경 중 외부 콜백이 액터나 다른 컴포넌트를 제거할 수 있다.
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ALSOutpost::PrepareForDeath()
{
	// 충돌 정리가 동기 콜백에서 액터를 제거해도 함락 사실은 월드에 남는다.
	if (!bCaptured)
	{
		bCaptured = true;
		Capture.Outpost = this;
		Capture.OutpostName = GetFName();
		Capture.Location = GetActorLocation();
		if (ULSBattlefieldStateSubsystem* State = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>())
		{
			Capture.bIsCoreOutpost = State->GetCoreOutpost() == this;
			State->RecordOutpostCaptured(this);
		}
		CaptureTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ALSOutpost::BroadcastCapture);
	}
	Vision->NotifyCombatDeath();
	NeutralizeCombat();
}

bool ALSOutpost::TryGetCapture(FLSOutpostCaptureSnapshot& Out) const
{
	if (!bCaptured)
	{
		return false;
	}
	Out = Capture;
	return true;
}

void ALSOutpost::BroadcastCapture()
{
	if (IsValid(this) && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		OnOutpostCapturedEvent.Broadcast(Capture);
	}
}

void ALSOutpost::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(CaptureTimer);
	}
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
bool ALSOutpost::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ALSOutpost, Faction) && bFactionLocked)
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}

EDataValidationResult ALSOutpost::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FLSOutpostStatRow InspectedStats;
	const TCHAR* FailureReason = nullptr;
	if (LSCombatStats::InspectRow(StatsRow, InspectedStats, FailureReason) != ELSCombatStatsInitResult::Success)
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: StatsRow Table=%s Row=%s Reason=%s"),
			*GetPathName(), *GetPathNameSafe(StatsRow.DataTable), *StatsRow.RowName.ToString(), FailureReason)));
		Result = EDataValidationResult::Invalid;
	}
	const FString ConfigurationError = GetCombatConfigurationError();
	if (!ConfigurationError.IsEmpty())
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: %s"), *GetPathName(), *ConfigurationError)));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

