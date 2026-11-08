#include "ShellExperiment/LSShellObstacleResponseComponent.h"

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSPhysicalBlocker.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Maps/LSBattlefieldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/World.h"

ULSShellObstacleResponseComponent::ULSShellObstacleResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetCanEverAffectNavigation(false);
}

void ULSShellObstacleResponseComponent::BeginPlay()
{
	Super::BeginPlay();
	Behavior = GetOwner()->FindComponentByClass<ULSEnemyBehaviorComponent>();
	if (Behavior.IsValid()) Behavior->AddTickPrerequisiteComponent(this);
	LastProgressPosition = GetOwner()->GetActorLocation();
}

float ULSShellObstacleResponseComponent::ComputeClearingForce(float MassKg, float Friction, float Gravity, float Acceleration)
{
	if (!FMath::IsFinite(MassKg) || MassKg <= 0 || !FMath::IsFinite(Friction) || Friction < 0 ||
		!FMath::IsFinite(Gravity) || Gravity < 0 || !FMath::IsFinite(Acceleration) || Acceleration < 0) return 0;
	const float Force = MassKg * (Friction * Gravity + Acceleration);
	return FMath::IsFinite(Force) ? Force : 0;
}

TArray<UPrimitiveComponent*> ULSShellObstacleResponseComponent::CollectStructuralBodies(UPrimitiveComponent* Body)
{
	TArray<UPrimitiveComponent*> Bodies;
	TSet<UPrimitiveComponent*> Visited;
	if (!IsValid(Body) || !Body->IsQueryCollisionEnabled()) return Bodies;
	Bodies.Add(Body);
	Visited.Add(Body);
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		UPrimitiveComponent* Current = Bodies[Index];
		const auto* Reactive = Current->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
		if (!Reactive || Reactive->GetPhysicalBody() != Current) continue;
		for (UPrimitiveComponent* Other : Reactive->GetStructuralContactBodies())
		{
			if (!IsValid(Other) || !IsValid(Other->GetOwner()) || Other->GetOwner()->IsActorBeingDestroyed() ||
				!Other->IsQueryCollisionEnabled() || Visited.Contains(Other)) continue;
			const auto* OtherReactive = Other->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
			if (!OtherReactive || OtherReactive->GetPhysicalBody() != Other) continue;
			Visited.Add(Other);
			Bodies.Add(Other);
		}
	}
	return Bodies;
}

AActor* ULSShellObstacleResponseComponent::GetClearingAttackTarget() const
{
	AActor* Target = ClearingBody.IsValid() ? ClearingBody->GetOwner() : nullptr;
	return ULSCombatTargetLibrary::IsCombatAlive(Target) ? Target : nullptr;
}

bool ULSShellObstacleResponseComponent::CanRestoreBehavior() const
{
	if (!Behavior.IsValid() || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner()) ||
		Behavior->Role != ELSEnemyRole::Assault ||
		Behavior->GetState() == ELSEnemyState::Ended || Behavior->GetState() == ELSEnemyState::Dead ||
		Behavior->IsRegionallyDormant()) return false;
	if (const auto* Reactive = GetOwner()->FindComponentByClass<ULSShellReactiveComponent>(); Reactive && Reactive->IsForcedMotionActive()) return false;
	if (const auto* Material = GetOwner()->FindComponentByClass<ULSShellMaterialStateComponent>();
		Material && (Material->IsCooled() || Material->GetActiveBondCount() > 0)) return false;
	if (const auto* Siege = GetOwner()->FindComponentByClass<ULSShellSiegeController>(); Siege && Siege->Phase != ELSShellSiegePhase::Disabled) return false;
	const auto* Character = Cast<ACharacter>(GetOwner());
	return !Character || (!Character->GetCapsuleComponent()->IsSimulatingPhysics() &&
		Character->GetCharacterMovement()->IsComponentTickEnabled() && Character->GetCharacterMovement()->MovementMode != MOVE_None);
}

void ULSShellObstacleResponseComponent::RestoreBehavior()
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (bRestorePending && bBehaviorWasTicking && Coordinator && Coordinator->IsBattleActive() && CanRestoreBehavior())
	{
		Behavior->SetComponentTickEnabled(true);
		bRestorePending = false;
		bBehaviorWasTicking = false;
	}
}

void ULSShellObstacleResponseComponent::FinishResponse(const FString& Reason)
{
	if (ClearingBody.IsValid())
	{
		LastAttemptedBody = ClearingBody;
		LastAttemptPosition = ClearingBody->GetComponentLocation();
		UE_LOG(LogTemp, Display, TEXT("SHELL_OBSTACLE_RESPONSE End=%s Blocker=%s Duration=%.3f Force=%.3f"),
			*Reason, *ClearingBody->GetPathName(), ResponseSeconds, LastClearingForce);
	}
	if (auto* Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>()) Attack->StopAttack();
	ClearingBody.Reset();
	ClearingProbeRoute.Reset();
	ClearingProbeSource.Reset();
	ResponseSeconds = ForceSeconds = NoProgressSeconds = 0;
	LastResponse = Reason;
	bRestorePending = bBehaviorWasTicking;
	RestoreBehavior();
	RequestAlternativeRoute(Reason);
	bAttemptedAlternative = true;
}

TArray<FVector> ULSShellObstacleResponseComponent::ReadProbeRoute()
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const auto* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Following ? Following->GetPath() : FNavPathSharedPtr();
	TArray<FVector> Route;
	if (!Character) return Route;
	const FVector Height(0, 0, Character->GetActorLocation().Z - Character->GetNavAgentLocation().Z);
	if (Path.IsValid() && Path->IsValid())
	{
		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		ObservedMoveGoal = Path->GetEndLocation();
		bHasObservedMoveGoal = FAISystem::IsValidLocation(ObservedMoveGoal);
		Route.Add(Character->GetActorLocation());
		for (int32 Index = FMath::Clamp(static_cast<int32>(Following->GetNextPathIndex()), 1, Points.Num() - 1); Index < Points.Num(); ++Index)
			Route.Add(Points[Index].Location + Height);
		LastProbeSource = TEXT("native path points");
	}
	else if (ClearingBody.IsValid() && ClearingProbeRoute.Num() > 1)
	{
		Route = ClearingProbeRoute;
		Route[0] = Character->GetActorLocation();
		LastProbeSource = TEXT("controlled-stop snapshot: ") + ClearingProbeSource;
	}
	else if (bHasObservedMoveGoal)
	{
		Route = {Character->GetActorLocation(), ObservedMoveGoal + Height};
		LastProbeSource = TEXT("no native path; short probe toward previously observed request goal");
	}
	else LastProbeSource = TEXT("no observed native request goal; no straight strategic-target substitution");
	return Route;
}

UPrimitiveComponent* ULSShellObstacleResponseComponent::FindFirstBlocker(FVector& OutCorridorDirection)
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return nullptr;
	LastProbeRoute = ReadProbeRoute();
	float Remaining = ProbeDistance;
	const FCollisionShape Shape = Character->GetCapsuleComponent()->GetCollisionShape(-1);
	for (int32 Index = 1; Index < LastProbeRoute.Num() && Remaining > 0; ++Index)
	{
		const FVector Start = LastProbeRoute[Index - 1];
		const FVector Segment = LastProbeRoute[Index] - Start;
		const double Length = Segment.Size();
		if (Length <= UE_KINDA_SMALL_NUMBER) continue;
		const FVector End = Start + Segment * FMath::Min(1., Remaining / Length);
		Remaining -= FVector::Distance(Start, End);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellBlockedCorridor), false, GetOwner());
		Query.bFindInitialOverlaps = true;
		FHitResult Hit;
		while (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
			LSCollisionChannels::ObstacleProjectile, Shape, Query))
		{
			UPrimitiveComponent* Component = Hit.GetComponent();
			if (!IsValid(Component)) return nullptr;
			if (Component->GetCollisionObjectType() == LSCollisionChannels::Ground)
			{
				Query.AddIgnoredComponent(Component);
				continue;
			}
			OutCorridorDirection = Segment.GetSafeNormal2D();
			ProbeSegmentOrigin = Start;
			return LSPhysicalBlocker::IsBlockingBody(Component) ? Component : nullptr;
		}
	}
	return nullptr;
}

void ULSShellObstacleResponseComponent::RequestAlternativeRoute(const FString& Reason)
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive() || !CanRestoreBehavior()) return;
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	if (const auto* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr)
		if (const FNavPathSharedPtr Path = Following->GetPath(); Path.IsValid()) Path->Invalidate();
	bLastAlternativePathExists = false;
	if (Character && bHasObservedMoveGoal)
		if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			if (const ANavigationData* Data = Nav->GetNavDataForProps(Character->GetNavAgentPropertiesRef(), Character->GetNavAgentLocation()))
			{
				FPathFindingQuery Query(Character, *Data, Character->GetNavAgentLocation(), ObservedMoveGoal);
				Query.SetAllowPartialPaths(false);
				const FPathFindingResult Result = Nav->FindPathSync(Character->GetNavAgentPropertiesRef(), Query);
				bLastAlternativePathExists = Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid() && !Result.Path->IsPartial();
				++ReplanRequests;
			}
	// 새 이동 명령은 발행하지 않는다. 기존 Enemy가 소유한 goal·요청 ID·경계 규칙으로 다음 판단에서 이동한다.
	UE_LOG(LogTemp, Display, TEXT("SHELL_OBSTACLE_REPLAN Reason=%s Requests=%d CompleteAlternative=%d HasObservedGoal=%d Goal=%s"),
		*Reason, ReplanRequests, bLastAlternativePathExists, bHasObservedMoveGoal, *ObservedMoveGoal.ToCompactString());
}

bool ULSShellObstacleResponseComponent::ReadGround(UPrimitiveComponent* Body, FVector Location, float& OutFriction, float& OutGroundZ) const
{
	FCollisionObjectQueryParams Objects(LSCollisionChannels::Ground);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellClearingGround), false, Body->GetOwner());
	FHitResult Hit;
	const double Height = FMath::Max(500., Body->Bounds.BoxExtent.Z + 100);
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector::UpVector * Height,
		Location - FVector::UpVector * Height, Objects, Query) || Hit.ImpactNormal.Z < .98) return false;
	const FBodyInstance* BodyInstance = Body->GetBodyInstance();
	const FBodyInstance* GroundInstance = Hit.GetComponent() ? Hit.GetComponent()->GetBodyInstance() : nullptr;
	const UPhysicalMaterial* Material = BodyInstance ? BodyInstance->GetSimplePhysicalMaterial() : nullptr;
	const UPhysicalMaterial* Ground = GroundInstance ? GroundInstance->GetSimplePhysicalMaterial() : nullptr;
	if (!Material || !Ground || !Material->bOverrideFrictionCombineMode || !Ground->bOverrideFrictionCombineMode ||
		Material->FrictionCombineMode != EFrictionCombineMode::Min || Ground->FrictionCombineMode != EFrictionCombineMode::Min) return false;
	OutFriction = FMath::Min(FMath::Max(Material->Friction, Material->StaticFriction), FMath::Max(Ground->Friction, Ground->StaticFriction));
	OutGroundZ = Hit.ImpactPoint.Z;
	return true;
}

bool ULSShellObstacleResponseComponent::ReadClearingAssembly(UPrimitiveComponent* Body, float& OutMass, float& OutFriction, FVector& OutCenter) const
{
	OutMass = OutFriction = 0;
	OutCenter = FVector::ZeroVector;
	bool bGrounded = false;
	float SupportZ = 0;
	for (UPrimitiveComponent* Member : CollectStructuralBodies(Body))
	{
		if (!Member->IsSimulatingPhysics()) return false;
		const float Mass = Member->GetMass();
		if (!FMath::IsFinite(Mass) || Mass <= 0) return false;
		OutMass += Mass;
		OutCenter += Member->GetCenterOfMass() * Mass;
		float Friction, GroundZ;
		if (ReadGround(Member, Member->GetComponentLocation(), Friction, GroundZ) &&
			FMath::Abs(Member->Bounds.GetBox().Min.Z - GroundZ) <= 3)
		{
			if (bGrounded && FMath::Abs(GroundZ - SupportZ) > 3) return false;
			SupportZ = GroundZ;
			bGrounded = true;
			// 접지 부위 중 큰 계수로 총중량의 저항을 상한 잡는다. 공중 화물의 계수를 지면 마찰로 더하지 않는다.
			OutFriction = FMath::Max(OutFriction, Friction);
		}
	}
	if (!bGrounded || !FMath::IsFinite(OutMass) || OutMass <= 0) return false;
	OutCenter /= OutMass;
	return true;
}

bool ULSShellObstacleResponseComponent::FindLateralClearance(UPrimitiveComponent* Body, FVector CorridorDirection, FVector& OutDestination) const
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Body->IsSimulatingPhysics()) return false;
	const TArray<UPrimitiveComponent*> Members = CollectStructuralBodies(Body);
	FBox AssemblyBounds(ForceInit);
	for (UPrimitiveComponent* Member : Members)
	{
		if (!Member->IsSimulatingPhysics()) return false;
		AssemblyBounds += Member->Bounds.GetBox();
	}
	if (!AssemblyBounds.IsValid) return false;
	const FVector Side(-CorridorDirection.Y, CorridorDirection.X, 0);
	const FVector Extent = AssemblyBounds.GetExtent();
	const float BodyRadius = FMath::Abs(Side.X) * Extent.X + FMath::Abs(Side.Y) * Extent.Y;
	const float Offset = FVector::DotProduct(AssemblyBounds.GetCenter() - ProbeSegmentOrigin, Side);
	const float RequiredOffset = BodyRadius + Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10;
	FCollisionObjectQueryParams Objects;
	for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, ECC_PhysicsBody,
		LSCollisionChannels::Unit, LSCollisionChannels::Building, LSCollisionChannels::PathObstacle}) Objects.AddObjectTypesToQuery(Channel);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellClearingSpace), false, GetOwner());
	for (UPrimitiveComponent* Member : Members) Query.AddIgnoredComponent(Member);
	Query.bFindInitialOverlaps = true;
	float Friction, GroundZ;
	if (!ReadGround(Body, Body->GetComponentLocation(), Friction, GroundZ)) return false;
	const float Preferred = Offset < 0 ? -1.f : 1.f;
	for (float Sign : {Preferred, -Preferred})
	{
		const FVector Destination = Body->GetComponentLocation() + Side * (Sign * RequiredOffset - Offset);
		if (const auto* Settings = ALSBattlefieldSettings::Find(GetWorld()))
		{
			const FBox DestinationBox = AssemblyBounds.ShiftBy(Destination - Body->GetComponentLocation());
			const FBox2D Bounds = Settings->GetWorldBounds();
			if (DestinationBox.Min.X < Bounds.Min.X || DestinationBox.Max.X > Bounds.Max.X ||
				DestinationBox.Min.Y < Bounds.Min.Y || DestinationBox.Max.Y > Bounds.Max.Y) continue;
		}
		float DestinationFriction, DestinationZ;
		if (!ReadGround(Body, Destination, DestinationFriction, DestinationZ) || FMath::Abs(DestinationZ - GroundZ) > 3) continue;
		bool bSupported = true;
		for (float X : {-1.f, 1.f})
			for (float Y : {-1.f, 1.f})
			{
				float CornerFriction, CornerZ;
				const FVector BoundsOffset = AssemblyBounds.GetCenter() - Body->GetComponentLocation();
				if (!ReadGround(Body, Destination + FVector(BoundsOffset.X + X * Extent.X, BoundsOffset.Y + Y * Extent.Y, 0), CornerFriction, CornerZ) ||
					FMath::Abs(CornerZ - GroundZ) > 3) bSupported = false;
			}
		if (!bSupported) continue;
		FHitResult Hit;
		bool bClear = true;
		for (UPrimitiveComponent* Member : Members)
			if (GetWorld()->SweepSingleByObjectType(Hit, Member->GetComponentLocation(),
				Member->GetComponentLocation() + Destination - Body->GetComponentLocation(), Member->GetComponentQuat(),
				Objects, Member->GetCollisionShape(1), Query)) { bClear = false; break; }
		if (bClear)
		{
			OutDestination = Destination;
			return true;
		}
	}
	return false;
}

void ULSShellObstacleResponseComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	if (bBehaviorWasTicking && !ClearingBody.IsValid() && !bRestorePending)
		FinishResponse(TEXT("blocker removed"));
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsSupplyExperiment() || !Coordinator->IsBattleActive() || !CanRestoreBehavior())
	{
		if (ClearingBody.IsValid()) FinishResponse(TEXT("inactive or externally controlled"));
		NoProgressSeconds = 0;
		return;
	}
	RestoreBehavior();
	FVector Direction;
	UPrimitiveComponent* Blocker = FindFirstBlocker(Direction);
	if (ClearingBody.IsValid())
	{
		if (Blocker != ClearingBody.Get()) { FinishResponse(TEXT("corridor changed")); return; }
		ResponseSeconds += FMath::Max(0.f, Delta);
		if (AActor* LivingTarget = GetClearingAttackTarget())
		{
			auto* Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
			if (!Attack || Attack->GetAttackSettings().Damage <= 0 || !Attack->StartPhysicalBlockerAttack(LivingTarget))
			{
				FinishResponse(TEXT("common blocker attack rejected"));
				return;
			}
		}
		else
		{
			const float Step = FMath::Min(FMath::Max(0.f, Delta), FMath::Max(0.f, FMath::Min(2.f, MaximumResponseSeconds) - ForceSeconds));
			ForceSeconds += Step;
			float Friction, AssemblyMass;
			FVector AssemblyCenter;
			FVector ValidatedDestination;
			if (!ReadClearingAssembly(Blocker, AssemblyMass, Friction, AssemblyCenter) ||
				!FindLateralClearance(Blocker, Direction, ValidatedDestination))
			{
				FinishResponse(TEXT("lateral clearance or material changed"));
				return;
			}
			ClearingDestination = ValidatedDestination;
			const FVector ForceDirection = (ClearingDestination - Blocker->GetComponentLocation()).GetSafeNormal2D();
			LastAssemblyMassKg = AssemblyMass;
			LastClearingForce = ComputeClearingForce(AssemblyMass, Friction, FMath::Abs(GetWorld()->GetGravityZ()), ClearingAcceleration);
			if (Step > 0 && Delta > 0 && LastClearingForce > 0)
			{
				Blocker->WakeAllRigidBodies();
				Blocker->AddForceAtLocation(ForceDirection * LastClearingForce * (Step / Delta), AssemblyCenter);
				for (UPrimitiveComponent* Member : CollectStructuralBodies(Blocker))
					if (auto* Reactive = Member->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>()) Reactive->NotifyExternalForce();
			}
			if (ForceSeconds >= FMath::Min(2.f, MaximumResponseSeconds)) FinishResponse(TEXT("bounded response completed; replan required"));
		}
		return;
	}
	if (!Behavior.IsValid() || !Behavior->IsComponentTickEnabled() || Behavior->GetState() == ELSEnemyState::Engaging ||
		Behavior->GetState() == ELSEnemyState::WaitingForSetup) { NoProgressSeconds = 0; return; }
	if (FVector::Dist2D(GetOwner()->GetActorLocation(), LastProgressPosition) >= ProgressDistance)
	{
		NoProgressSeconds = 0;
		LastAttemptedBody.Reset();
		bAttemptedAlternative = false;
		LastProgressPosition = GetOwner()->GetActorLocation();
	}
	else NoProgressSeconds += FMath::Max(0.f, Delta);
	if (!Blocker || NoProgressSeconds < BlockedSeconds) return;
	if (ULSCombatTargetLibrary::IsCombatAlive(Blocker->GetOwner()))
	{
		const auto* Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
		if (!Attack || ULSCombatTargetLibrary::GetAttackDistance(GetOwner(), Blocker->GetOwner()) > Attack->GetAttackSettings().Range)
		{
			if (!bAttemptedAlternative)
			{
				RequestAlternativeRoute(TEXT("living blocker outside common attack range"));
				bAttemptedAlternative = true;
			}
			LastResponse = bLastAlternativePathExists ? TEXT("living blocker out of range; complete alternative found") : TEXT("living blocker out of range; path failure remains");
			return;
		}
	}
	if (LastAttemptedBody.Get() == Blocker && FVector::DistSquared(LastAttemptPosition, Blocker->GetComponentLocation()) <= 100)
	{
		if (!bAttemptedAlternative)
		{
			RequestAlternativeRoute(TEXT("unchanged blocker after bounded clearing"));
			bAttemptedAlternative = true;
		}
		LastResponse = bLastAlternativePathExists ? TEXT("complete alternative found; waiting for normal route progress") : TEXT("no complete alternative; path failure remains");
		return;
	}
	if (!ULSCombatTargetLibrary::IsCombatAlive(Blocker->GetOwner()) && !FindLateralClearance(Blocker, Direction, ClearingDestination))
	{
		LastAttemptedBody = Blocker;
		LastAttemptPosition = Blocker->GetComponentLocation();
		RequestAlternativeRoute(TEXT("no verified lateral clearance"));
		bAttemptedAlternative = true;
		LastResponse = bLastAlternativePathExists ? TEXT("complete alternative found after clearance rejection") : TEXT("no clearance or complete alternative; path failure remains");
		return;
	}
	ClearingBody = Blocker;
	ClearingProbeRoute = LastProbeRoute;
	ClearingProbeSource = LastProbeSource;
	bAttemptedAlternative = false;
	ResponseSeconds = ForceSeconds = LastClearingForce = 0;
	++ResponseCount;
	bBehaviorWasTicking = Behavior->IsComponentTickEnabled();
	Behavior->SetComponentTickEnabled(false);
	if (auto* Pawn = Cast<APawn>(GetOwner())) if (auto* Controller = Cast<AAIController>(Pawn->GetController())) Controller->StopMovement();
	LastResponse = TEXT("bounded blocker response started");
	UE_LOG(LogTemp, Display, TEXT("SHELL_OBSTACLE_RESPONSE Begin Owner=%s Blocker=%s Living=%d Destination=%s"),
		*GetOwner()->GetPathName(), *Blocker->GetPathName(), ULSCombatTargetLibrary::IsCombatAlive(Blocker->GetOwner()),
		*ClearingDestination.ToCompactString());
}

void ULSShellObstacleResponseComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ClearingBody.IsValid()) FinishResponse(TEXT("owner removed"));
	if (Behavior.IsValid()) Behavior->RemoveTickPrerequisiteComponent(this);
	Super::EndPlay(Reason);
}
