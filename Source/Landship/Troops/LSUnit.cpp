#include "Troops/LSUnit.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "AIController.h"

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
#include "Engine/World.h"
#include "UObject/UnrealType.h"

ALSUnit::ALSUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Selection->Kind = ELSSelectionKind::Unit;
	Orders = CreateDefaultSubobject<ULSOrderComponent>(TEXT("Orders"));
	BasicAttack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("BasicAttack"));
	BasicAttack->SetAutoAttackOnBeginPlay(false);
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Health->bDestroyOnDeath = true;
	GetCapsuleComponent()->SetCollisionObjectType(LSCollisionChannels::Unit);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ALSUnit::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	bFactionLocked = true;
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (!Health->InitializeHealth(StatsInitResult, Stats.Health,
		FLSPrepareCombatDeath::CreateUObject(this, &ALSUnit::PrepareForDeath), GetCombatConfigurationError()))
	{
		// 준비 실패는 사망이 아니므로 알림·랙돌·제거 없이 실행과 방해 충돌만 막는다.
		NeutralizeCombat();
	}
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
			"[{Function}] {Owner} stats initialized: Health={Health} AttackPower={AttackPower} AttackInterval={AttackInterval} AttackRange={AttackRange} DetectionRange={DetectionRange} VisionRadius={VisionRadius} MovementSpeed={MovementSpeed}",
			("Function", __FUNCTION__), ("Owner", GetPathName()),
			("Health", Stats.Health),
			("AttackPower", Stats.AttackPower),
			("AttackInterval", Stats.AttackInterval),
			("AttackRange", Stats.AttackRange),
			("DetectionRange", Stats.DetectionRange),
			("VisionRadius", Stats.VisionRadius),
			("MovementSpeed", Stats.MovementSpeed));
	}
	Selection->SetPlayerControlled(GetFaction() == ELSFaction::Ally);
	if (Health->GetInitializationState() == ELSHealthInitializationState::Ready)
	{
		FLSBasicAttackSettings AttackSettings;
		AttackSettings.Damage = Stats.AttackPower;
		AttackSettings.Interval = Stats.AttackInterval;
		AttackSettings.Range = Stats.AttackRange;
		BasicAttack->ConfigureAttack(AttackSettings);
		GetCharacterMovement()->MaxWalkSpeed = Stats.MovementSpeed;
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

void ALSUnit::Restart()
{
	Super::Restart();
	if (!IsValid(this) || IsActorBeingDestroyed())
	{
		return;
	}
	FLSHealthSnapshot Snapshot;
	if (Health->GetInitializationState() == ELSHealthInitializationState::Failed
		|| (Health->TryGetHealth(Snapshot) && Snapshot.bIsDead))
	{
		// 재빙의의 Restart가 기본 이동 모드를 복원한다. 시체의 물리는 유지하고 이동만 다시 막는다.
		StopCombatMovement();
	}
}

void ALSUnit::StopCombatMovement()
{
	if (AController* UnitController = GetController())
	{
		UnitController->StopMovement();
	}
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
}

void ALSUnit::NeutralizeCombat()
{
	BasicAttack->StopAttack();

	StopCombatMovement();
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!IsValid(Primitive))
		{
			continue;
		}
		Primitive->SetCanEverAffectNavigation(false);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// 충돌 변경은 동기 overlap 콜백에서 소유자를 제거하거나 월드를 종료할 수 있다.
		if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
		{
			return;
		}
	}
}

void ALSUnit::PrepareForDeath()
{
	NeutralizeCombat();
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	if (bUseRagdoll)
	{
		USkeletalMeshComponent* UnitMesh = GetMesh();
		const auto CanContinueRagdoll = [this, UnitMesh]
		{
			return IsValid(this) && !IsActorBeingDestroyed() && IsValid(UnitMesh)
				&& GetWorld() && !GetWorld()->bIsTearingDown;
		};
		if (!CanContinueRagdoll()) { return; }
		UnitMesh->SetCollisionObjectType(ECC_PhysicsBody);
		if (!CanContinueRagdoll()) { return; }
		UnitMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		if (!CanContinueRagdoll()) { return; }
		UnitMesh->SetCollisionResponseToChannel(LSCollisionChannels::Ground, ECR_Block);
		if (!CanContinueRagdoll()) { return; }
		UnitMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		if (!CanContinueRagdoll()) { return; }
		UnitMesh->SetSimulatePhysics(true);
	}
}

#if WITH_EDITOR
bool ALSUnit::CanEditChange(const FProperty* Property) const
{
	return !(bFactionLocked && Property->GetFName() == GET_MEMBER_NAME_CHECKED(ALSUnit, Faction))
		&& Super::CanEditChange(Property);
}

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

