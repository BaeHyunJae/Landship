#include "Troops/LSUnit.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/DataValidation.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Components/CapsuleComponent.h"
#include "Logging/StructuredLog.h"

ALSUnit::ALSUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Health->bDestroyOnDeath = true;
	GetCapsuleComponent()->SetCollisionObjectType(LSCollisionChannels::Unit);
}

void ALSUnit::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	bFactionLocked = true;
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (!Health->InitializeHealth(StatsInitResult, Stats.Health,
		FLSPrepareCombatDeath::CreateUObject(this, &ALSUnit::PrepareForDeath), GetCombatConfigurationError()))
	{
		NeutralizeCombat();
	}
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} initialized: Health={Health} AttackPower={AttackPower} AttackInterval={AttackInterval} AttackRange={AttackRange} DetectionRange={DetectionRange} VisionRadius={VisionRadius} MovementSpeed={MovementSpeed}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("AttackPower", Stats.AttackPower),
			("AttackInterval", Stats.AttackInterval),
			("AttackRange", Stats.AttackRange),
			("DetectionRange", Stats.DetectionRange),
			("VisionRadius", Stats.VisionRadius),
			("MovementSpeed", Stats.MovementSpeed));
	}
	Super::BeginPlay();
}

bool ALSUnit::TryGetStats(FLSUnitStatRow& OutStats) const
{
	if (!IsStatsInitialized())
	{
		return false;
	}
	OutStats = Stats;
	return true;
}


void ALSUnit::SetInitialFaction(ELSFaction InFaction)
{
	if (!ensureAlwaysMsgf(!bFactionLocked, TEXT("[LSUnit::SetInitialFaction] Faction is locked: Owner=%s"), *GetPathName()))
	{
		return;
	}
	Faction = InFaction;
}

FString ALSUnit::GetCombatConfigurationError() const
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
	if (bUseRagdoll)
	{
		const USkeletalMeshComponent* UnitMesh = GetMesh();
		if (!UnitMesh || !UnitMesh->GetSkeletalMeshAsset())
		{
			return TEXT("Ragdoll requires a skeletal mesh");
		}
		const UPhysicsAsset* PhysicsAsset = UnitMesh->GetPhysicsAsset();
		if (!PhysicsAsset || PhysicsAsset->SkeletalBodySetups.IsEmpty())
		{
			return TEXT("Ragdoll requires a physics asset with bodies");
		}
	}
	return FString();
}

void ALSUnit::NeutralizeCombat()
{
	if (AController* UnitController = GetController())
	{
		UnitController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetCanEverAffectNavigation(false);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void ALSUnit::PrepareForDeath()
{
	NeutralizeCombat();
	if (bUseRagdoll)
	{
		USkeletalMeshComponent* UnitMesh = GetMesh();
		UnitMesh->SetCollisionObjectType(ECC_PhysicsBody);
		UnitMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		UnitMesh->SetCollisionResponseToChannel(LSCollisionChannels::Ground, ECR_Block);
		UnitMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		UnitMesh->SetSimulatePhysics(true);
	}
}

#if WITH_EDITOR
EDataValidationResult ALSUnit::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FLSUnitStatRow InspectedStats;
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

