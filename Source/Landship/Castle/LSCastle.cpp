#include "Castle/LSCastle.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/DataValidation.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"

ALSCastle::ALSCastle()
{
	PrimaryActorTick.bCanEverTick = false;
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
}

void ALSCastle::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	bFactionLocked = true;
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (!Health->InitializeHealth(StatsInitResult, Stats.Health,
		FLSPrepareCombatDeath::CreateUObject(this, &ALSCastle::PrepareForDeath), GetCombatConfigurationError()))
	{
		NeutralizeCombat();
	}
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} initialized: Health={Health} VisionRadius={VisionRadius} MovementSpeed={MovementSpeed}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("VisionRadius", Stats.VisionRadius),
			("MovementSpeed", Stats.MovementSpeed));
	}
	Super::BeginPlay();
}

bool ALSCastle::TryGetStats(FLSCastleStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}


void ALSCastle::SetInitialFaction(ELSFaction InFaction)
{
	if (!ensureAlwaysMsgf(!bFactionLocked, TEXT("[LSCastle::SetInitialFaction] Faction is locked: Owner=%s"), *GetPathName()))
	{
		return;
	}
	Faction = InFaction;
}

FString ALSCastle::GetCombatConfigurationError() const
{
	if (!LSFaction::IsValid(Faction))
	{
		return TEXT("Invalid faction");
	}
	TInlineComponentArray<ULSHealthComponent*> HealthComponents(this);
	if (HealthComponents.Num() != 1 || HealthComponents[0] != Health)
	{
		return TEXT("Exactly one owned health subobject is required");
	}
	return FString();
}

void ALSCastle::NeutralizeCombat()
{
	const auto CanContinue = [this]()
	{
		return IsValid(this) && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown;
	};
	if (!CanContinue())
	{
		return;
	}
	if (AController* CastleController = GetController())
	{
		CastleController->StopMovement();
	}
	if (!CanContinue())
	{
		return;
	}
	if (UPawnMovementComponent* Movement = GetMovementComponent())
	{
		Movement->StopMovementImmediately();
		if (!CanContinue())
		{
			return;
		}
		if (IsValid(Movement))
		{
			Movement->Deactivate();
		}
	}
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

void ALSCastle::PrepareForDeath()
{
	NeutralizeCombat();
}

#if WITH_EDITOR
EDataValidationResult ALSCastle::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FLSCastleStatRow InspectedStats;
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

