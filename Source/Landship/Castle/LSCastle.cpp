#include "Castle/LSCastle.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "AIController.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/DataValidation.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Logging/StructuredLog.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"

ALSCastle::ALSCastle()
{
	PrimaryActorTick.bCanEverTick = false;
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Selection->Kind = ELSSelectionKind::Castle;
	Orders = CreateDefaultSubobject<ULSOrderComponent>(TEXT("Orders"));
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(100.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionObjectType(LSCollisionChannels::Building);
	Collision->SetCanEverAffectNavigation(false);
	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->NavAgentProps.AgentRadius = 142.0f;
	Movement->NavAgentProps.AgentHeight = 200.0f;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ALSCastle::BeginPlay()
{
	// 소유 컴포넌트와 Blueprint의 BeginPlay보다 먼저 조회 결과를 확정한다.
	bFactionLocked = true;
	StatsInitResult = LSCombatStats::TryReadRow(StatsRow, this, Stats);
	if (!Health->InitializeHealth(StatsInitResult, Stats.Health,
	                              FLSPrepareCombatDeath::CreateUObject(this, &ALSCastle::PrepareForDeath),
	                              GetCombatConfigurationError()))
	{
		NeutralizeCombat();
	}
	if (IsStatsInitialized())
	{
		UE_LOGFMT(LogLSCombatStats, Display,
		          "[{Function}] {Owner} stats initialized: Health={Health} VisionRadius={VisionRadius} "
		          "MovementSpeed={MovementSpeed}",
		          ("Function", __FUNCTION__), ("Owner", GetPathName()), ("Health", Stats.Health),
		          ("VisionRadius", Stats.VisionRadius), ("MovementSpeed", Stats.MovementSpeed));
	}
	Selection->SetPlayerControlled(GetFaction() == ELSFaction::Ally);
	Movement->MaxSpeed =
	    Health->GetInitializationState() == ELSHealthInitializationState::Ready ? Stats.MovementSpeed : 0.0f;
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
	if (!ensureAlwaysMsgf(!bFactionLocked, TEXT("[LSCastle::SetInitialFaction] Faction is locked: Owner=%s"),
	                      *GetPathName()))
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
	{ return IsValid(this) && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown; };
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
	if (UPawnMovementComponent* PawnMovement = GetMovementComponent())
	{
		PawnMovement->StopMovementImmediately();
		if (!CanContinue())
		{
			return;
		}
		if (IsValid(PawnMovement))
		{
			PawnMovement->Deactivate();
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
bool ALSCastle::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ALSCastle, Faction) && bFactionLocked)
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}

EDataValidationResult ALSCastle::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FLSCastleStatRow InspectedStats;
	const TCHAR* FailureReason = nullptr;
	if (LSCombatStats::InspectRow(StatsRow, InspectedStats, FailureReason) != ELSCombatStatsInitResult::Success)
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: StatsRow Table=%s Row=%s Reason=%s"),
		                                                   *GetPathName(), *GetPathNameSafe(StatsRow.DataTable),
		                                                   *StatsRow.RowName.ToString(), FailureReason)));
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

UPawnMovementComponent* ALSCastle::GetMovementComponent() const
{
	return Movement;
}

bool ALSCastle::RequestMove(const FVector& Destination)
{
	AAIController* AI = Cast<AAIController>(GetController());
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Destination.ContainsNaN() || !AI || !Navigation || !ULSCombatTargetLibrary::IsCombatAlive(this) ||
	    !Movement->IsActive() || Movement->MaxSpeed <= 0.0f)
	{
		return false;
	}
	FNavLocation Goal;
	if (!Navigation->ProjectPointToNavigation(Destination, Goal, FVector(100.0f, 100.0f, 250.0f)))
	{
		return false;
	}
	// 도달 불가능한 명령이 진행 중인 이동을 취소하지 않도록 먼저 경로를 검사한다.
	UNavigationPath* Path =
	    Navigation->FindPathToLocationSynchronously(GetWorld(), GetNavAgentLocation(), Goal.Location, this);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		return false;
	}
	return AI->MoveToLocation(Goal.Location, AcceptanceRadius, true, true, false, false, nullptr, false) !=
	       EPathFollowingRequestResult::Failed;
}

bool ALSCastle::IsMoving() const
{
	const AAIController* AI = Cast<AAIController>(GetController());
	return AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle;
}

void ALSCastle::StopMoving()
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	Movement->StopMovementImmediately();
}
