#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "ShellExperiment/LSShellDetonationCue.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Maps/LSBattlefieldSettings.h"

namespace
{
	uint64 AllocateContactEpoch()
	{
		static uint64 NextContactEpoch = 1;
		return NextContactEpoch++;
	}
}

ULSShellReactiveComponent::ULSShellReactiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void ULSShellReactiveComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	SetPhysicalBody(Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()));
	static uint64 NextBodyId = 1;
	BodyId = NextBodyId++;
	FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::ClearConsumedMomentum);
	Behavior = GetOwner()->FindComponentByClass<ULSEnemyBehaviorComponent>();
	if (Character.IsValid())
	{
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
		Character->LandedDelegate.AddDynamic(this, &ThisClass::OnLanded);
	}
	if (ULSHealthComponent* Health = GetOwner()->FindComponentByClass<ULSHealthComponent>())
		Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnDied);
}

void ULSShellReactiveComponent::SetPhysicalBody(UPrimitiveComponent* InBody)
{
	if (Body.Get() != InBody)
	{
		if (Body.IsValid()) Body->OnComponentHit.RemoveAll(this);
		ClearContactRelations();
		ClearCharge();
		RestoreThrustPropelled(false);
		if (StoredChargeCue.IsValid()) StoredChargeCue->Destroy();
		StoredChargeCue.Reset();
		ArenaGatherCommands.Reset();
		Body = InBody;
		++BodyGeneration;
		PendingMomentumVelocity = ConsumedAfterSampleVelocity = FVector::ZeroVector;
	}
	else if (!InBody)
	{
		ClearContactRelations();
		ClearCharge();
	}
	if (HasBegunPlay() && Body.IsValid())
	{
		Body->SetNotifyRigidBodyCollision(true);
		Body->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnBodyHit);
	}
}

void ULSShellReactiveComponent::ClearCharge()
{
	ChargeStore.Clear();
	if (StoredChargeCue.IsValid()) StoredChargeCue->Destroy();
	StoredChargeCue.Reset();
}

float ULSShellReactiveComponent::AddCharge(const FLSShellChargePacket& Packet, float Capacity)
{
	if (!bCanStoreCharge || !IsPhysicalBodyPresent()) return 0;
	return ChargeStore.Add(Packet, Capacity, BodyId);
}

TArray<UPrimitiveComponent*> ULSShellReactiveComponent::GetContactBodies() const
{
	TArray<UPrimitiveComponent*> Result;
	for (const auto& Contact : Contacts)
		if (UPrimitiveComponent* OtherBody = Contact.Key.Get()) Result.Add(OtherBody);
	return Result;
}

bool ULSShellReactiveComponent::IsContactInsulated(UPrimitiveComponent* OtherBody) const
{
	const bool* bInsulated = StructuralContacts.Find(OtherBody);
	return bInsulated && *bInsulated;
}

TArray<UPrimitiveComponent*> ULSShellReactiveComponent::GetStructuralContactBodies() const
{
	TArray<UPrimitiveComponent*> Result;
	for (const auto& Contact : StructuralContacts)
		if (UPrimitiveComponent* OtherBody = Contact.Key.Get()) Result.Add(OtherBody);
	return Result;
}

uint64 ULSShellReactiveComponent::GetContinuingContactEpoch(UPrimitiveComponent* OtherBody) const
{
	const uint64* Epoch = Contacts.Find(OtherBody);
	return Epoch ? *Epoch : 0;
}

void ULSShellReactiveComponent::SetStructuralContact(ULSShellReactiveComponent* OtherReactive, bool bInsulated)
{
	if (!IsValid(OtherReactive) || OtherReactive == this || !IsPhysicalBodyPresent() ||
		!OtherReactive->IsPhysicalBodyPresent()) return;
	UPrimitiveComponent* OtherBody = OtherReactive->GetPhysicalBody();
	uint64 Epoch = GetContinuingContactEpoch(OtherBody);
	if (!Epoch) Epoch = OtherReactive->GetContinuingContactEpoch(Body.Get());
	if (!Epoch) Epoch = AllocateContactEpoch();
	Contacts.Add(OtherBody, Epoch);
	OtherReactive->Contacts.Add(Body, Epoch);
	StructuralContacts.Add(OtherBody, bInsulated);
	OtherReactive->StructuralContacts.Add(Body, bInsulated);
}

void ULSShellReactiveComponent::ReleaseStructuralContact(ULSShellReactiveComponent* OtherReactive)
{
	if (!IsValid(OtherReactive)) return;
	StructuralContacts.Remove(OtherReactive->GetPhysicalBody());
	OtherReactive->StructuralContacts.Remove(Body);
}

void ULSShellReactiveComponent::NotifyExternalForce()
{
	LastExternalForceFrame = FMath::Max<uint64>(1, GFrameCounter);
}

bool ULSShellReactiveComponent::IsPhysicalBodyPresent() const
{
	return IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && Body.IsValid() &&
		Body->IsQueryCollisionEnabled() && (Body->IsSimulatingPhysics() || ULSCombatTargetLibrary::IsCombatAlive(GetOwner()));
}

float ULSShellReactiveComponent::GetPhysicsMassKg() const
{
	if (Body.IsValid() && Body->IsSimulatingPhysics()) return Body->GetMass();
	if (Character.IsValid()) return Character->GetCharacterMovement()->Mass;
	return 100;
}

FVector ULSShellReactiveComponent::GetMomentumAdjustedLinearVelocity() const
{
	if (Body.IsValid() && Body->IsSimulatingPhysics())
	{
		const bool bAfterPhysics = GetWorld() && GetWorld()->bInTick && GetWorld()->TickGroup >= TG_PostPhysics;
		return Body->GetPhysicsLinearVelocity() + PendingMomentumVelocity - (bAfterPhysics ? ConsumedAfterSampleVelocity : FVector::ZeroVector);
	}
	if (Character.IsValid())
	{
		const auto* Movement = Character->GetCharacterMovement();
		return Movement->PendingLaunchVelocity.IsNearlyZero() ? Movement->Velocity : Movement->PendingLaunchVelocity;
	}
	return FVector::ZeroVector;
}

bool ULSShellReactiveComponent::CanReceiveMomentum() const
{
	return IsPhysicalBodyPresent() && (Body->IsSimulatingPhysics() || Character.IsValid());
}

FVector ULSShellReactiveComponent::GetSampledSurfaceVelocity(FVector Point) const
{
	return PreResponseVelocity + FVector::CrossProduct(PreResponseAngularVelocity, Point - PreResponseCenter);
}

bool ULSShellReactiveComponent::ApplyMomentum(FVector Impulse)
{
	ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive() || !CanReceiveMomentum() || Impulse.ContainsNaN() ||
		Impulse.IsNearlyZero()) return false;
	if (Body->IsSimulatingPhysics())
	{
		if (const auto* Siege = GetOwner()->FindComponentByClass<ULSShellSiegeController>()) Impulse /= Siege->GetResponseResistanceMultiplier();
		const FVector DeltaVelocity = Impulse / GetPhysicsMassKg();
		PendingMomentumVelocity += DeltaVelocity;
		PreResponseVelocity += DeltaVelocity;
		// 표본 이후, 같은 물리 스텝 전에 추가한 충격은 그 스텝 끝에서만 지운다.
		if (GetWorld()->bInTick && GetWorld()->TickGroup == TG_PrePhysics)
			ConsumedAfterSampleVelocity += DeltaVelocity;
		Body->WakeAllRigidBodies();
		Body->AddImpulse(Impulse);
		return true;
	}
	if (!Character.IsValid() || GetPhysicsMassKg() <= 0) return false;
	ApplyImpulseVelocity(Impulse / GetPhysicsMassKg());
	return true;
}

void ULSShellReactiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
	if (Body.IsValid()) Body->OnComponentHit.RemoveAll(this);
	if (Character.IsValid()) Character->LandedDelegate.RemoveAll(this);
	ClearContactRelations();
	ClearCharge();
	RestoreThrustPropelled(false);
	if (StoredChargeCue.IsValid()) StoredChargeCue->Destroy();
	StoredChargeCue.Reset();
	ArenaGatherCommands.Reset();
	Super::EndPlay(Reason);
}

void ULSShellReactiveComponent::MarkPropelledByThrust()
{
	RestoreThrustPropelled(true);
}

void ULSShellReactiveComponent::QueueArenaGather(AActor* Source, FVector TargetVelocity, FVector AssemblyVelocity, float Weight,
	float ResponseSeconds, float MaxAcceleration, float MaxSpeed, float Step)
{
	if (IsValid(Source) && Weight > 0 && Step > 0)
		ArenaGatherCommands.Add({Source, TargetVelocity, AssemblyVelocity, Weight, ResponseSeconds, MaxAcceleration, MaxSpeed, Step});
}

void ULSShellReactiveComponent::CancelArenaGather(AActor* Source)
{
	ArenaGatherCommands.RemoveAll([Source](const FArenaGatherCommand& Command) { return Command.Source.Get() == Source; });
}

void ULSShellReactiveComponent::ApplyArenaGather()
{
	FVector Target = FVector::ZeroVector, AssemblyVelocity = FVector::ZeroVector;
	float TotalWeight = 0, Response = 0, MaxAcceleration = 0, MaxSpeed = 0, Step = 0;
	for (const FArenaGatherCommand& Command : ArenaGatherCommands)
	{
		if (!Command.Source.IsValid()) continue;
		Target += Command.TargetVelocity * Command.Weight;
		AssemblyVelocity += Command.AssemblyVelocity * Command.Weight;
		TotalWeight += Command.Weight;
		Response += Command.ResponseSeconds * Command.Weight;
		MaxAcceleration += Command.MaxAcceleration * Command.Weight;
		MaxSpeed += Command.MaxSpeed * Command.Weight;
		Step = FMath::Max(Step, Command.Step);
	}
	ArenaGatherCommands.Reset();
	if (TotalWeight <= 0 || !CanReceiveMomentum()) return;
	Target = (Target / TotalWeight).GetClampedToMaxSize(MaxSpeed / TotalWeight);
	AssemblyVelocity /= TotalWeight;
	AssemblyVelocity.Z = 0;
	if (Character.IsValid() && !Body->IsSimulatingPhysics())
	{
		SuspendBehavior();
		bHasGravity = true;
		LastGravityFrame = GFrameCounter;
	}
	const FVector Delta = ((Target - AssemblyVelocity) * (Step / FMath::Max(Response / TotalWeight, KINDA_SMALL_NUMBER)))
		.GetClampedToMaxSize(MaxAcceleration / TotalWeight * Step);
	if (Delta.IsNearlyZero()) return;
	if (Body->IsSimulatingPhysics())
	{
		PendingMomentumVelocity += Delta;
		PreResponseVelocity += Delta;
		Body->WakeAllRigidBodies();
		Body->AddImpulse(Delta * GetPhysicsMassKg());
	}
	else if (Character.IsValid())
	{
		SuspendBehavior();
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		FVector Velocity = Movement->PendingLaunchVelocity.IsNearlyZero() ? Movement->Velocity : Movement->PendingLaunchVelocity;
		Velocity += Delta;
		PreResponseVelocity = Velocity;
		Character->LaunchCharacter(Velocity, true, true);
		LastImpulseFrame = GFrameCounter;
		bHasGravity = true;
		LastGravityFrame = GFrameCounter;
	}
	NotifyExternalForce();
}

void ULSShellReactiveComponent::UpdateArenaState(float Delta)
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	const bool bActive = Coordinator && Coordinator->IsBattleActive();
	if (!bActive || !IsPhysicalBodyPresent()) RestoreThrustPropelled(false);
	if (bThrustPropelled && bActive)
	{
		const auto& Tuning = Coordinator->SupplyTuning.Arena;
		bool bGrounded = Character.IsValid() && !Body->IsSimulatingPhysics() && Character->GetCharacterMovement()->IsMovingOnGround();
		if (!bGrounded)
		{
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
			Objects.AddObjectTypesToQuery(ECC_WorldStatic);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(ShellThrustGround), false, GetOwner());
			FHitResult Hit;
			bGrounded = GetWorld()->LineTraceSingleByObjectType(Hit, Body->Bounds.Origin,
				Body->Bounds.Origin - FVector(0, 0, Body->Bounds.BoxExtent.Z + 8), Objects, Query) && Hit.ImpactNormal.Z >= Tuning.LandingNormalZ;
		}
		PropelledStableSeconds = bGrounded && GetMomentumAdjustedLinearVelocity().Size() < Tuning.PropelledSettleSpeed
			? PropelledStableSeconds + Delta : 0;
		if (PropelledStableSeconds >= Tuning.PropelledSettleSeconds) RestoreThrustPropelled(false);
	}
	const bool bShow = bActive && Coordinator->SupplyTuning.Arena.bEnabled && IsPhysicalBodyPresent() && GetStoredCharge() > 0;
	if (!bShow && StoredChargeCue.IsValid()) { StoredChargeCue->Destroy(); StoredChargeCue.Reset(); }
	if (bShow && !StoredChargeCue.IsValid())
	{
		if (auto* Cue = GetWorld()->SpawnActor<ALSShellDetonationCue>())
		{
			StoredChargeCue = Cue;
			Cue->InitializeStoredCharge(this, Body.Get());
		}
	}
}

void ULSShellReactiveComponent::ClearConsumedMomentum(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld()) return;
	PendingMomentumVelocity -= ConsumedAfterSampleVelocity;
	ConsumedAfterSampleVelocity = FVector::ZeroVector;
}

bool ULSShellReactiveComponent::IsForcedMotionActive() const
{
	return Character.IsValid() && (bForcedMotion ||
		!Character->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
}

bool ULSShellReactiveComponent::CanMoveByGravity() const
{
	if (const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()); Coordinator && Coordinator->IsSupplyExperiment())
		return IsPhysicalBodyPresent() && (Body->IsSimulatingPhysics() || Character.IsValid());
	return Character.IsValid() && ULSCombatTargetLibrary::IsCombatAlive(GetOwner());
}

void ULSShellReactiveComponent::SuspendBehavior()
{
	if (bForcedMotion || !Character.IsValid()) return;
	bForcedMotion = true;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	FVector Velocity = Movement->Velocity;
	const FVector PendingLaunch = Movement->PendingLaunchVelocity;
	// 처음 외력에 넘길 때만 AI 보행을 멈춘다. 이미 외력으로 얻은 속도는 합산한다.
	if (Movement->IsMovingOnGround() && PendingLaunch.IsNearlyZero())
	{
		Velocity.X = 0;
		Velocity.Y = 0;
	}
	if (Behavior.IsValid())
	{
		bBehaviorWasTicking = Behavior->IsComponentTickEnabled();
		Behavior->SetComponentTickEnabled(false);
	}
	if (AController* Controller = Character->GetController()) Controller->StopMovement();
	if (ULSBasicAttackComponent* Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>()) Attack->StopAttack();
	bAvoidanceWasEnabled = Movement->bUseRVOAvoidance;
	Movement->SetAvoidanceEnabled(false);
	// 경로 중단이 지운 속도를 복원한 뒤 외력을 합산한다.
	Movement->Velocity = Velocity;
	Movement->PendingLaunchVelocity = PendingLaunch;
}

void ULSShellReactiveComponent::ApplyImpulseVelocity(FVector DeltaVelocity)
{
	ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Character.IsValid() || !Coordinator || !Coordinator->IsBattleActive() ||
		!ULSCombatTargetLibrary::IsCombatAlive(GetOwner()) || DeltaVelocity.ContainsNaN() ||
		!FMath::IsFinite(MovementResistance) || MovementResistance <= 0 || DeltaVelocity.IsNearlyZero()) return;
	SuspendBehavior();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector BaseVelocity = Movement->PendingLaunchVelocity.IsNearlyZero()
		? Movement->Velocity : Movement->PendingLaunchVelocity;
	const auto* Siege = GetOwner()->FindComponentByClass<ULSShellSiegeController>();
	const float SiegeResistance = Siege ? Siege->GetResponseResistanceMultiplier() : 1;
	PreResponseVelocity = BaseVelocity + DeltaVelocity / (MovementResistance * SiegeResistance);
	Character->LaunchCharacter(PreResponseVelocity, true, true);
	LastImpulseFrame = GFrameCounter;
}

void ULSShellReactiveComponent::ApplyGravity(FVector Center, float Acceleration, float DeltaSeconds)
{
	if (!FMath::IsFinite(Acceleration) || Acceleration <= 0 || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0 || Center.ContainsNaN()) return;
	ApplyImpulseVelocity((Center - GetOwner()->GetActorLocation()).GetSafeNormal() * Acceleration * DeltaSeconds);
	if (bForcedMotion)
	{
		NotifyExternalForce();
		bHasGravity = true;
		LastGravityFrame = GFrameCounter;
	}
}

void ULSShellReactiveComponent::RemoveContactRelation(UPrimitiveComponent* OtherBody)
{
	Contacts.Remove(OtherBody);
	StructuralContacts.Remove(OtherBody);
	if (!IsValid(OtherBody)) return;
	AActor* OtherOwner = OtherBody->GetOwner();
	if (!IsValid(OtherOwner)) return;
	ULSShellReactiveComponent* OtherReactive = OtherOwner->FindComponentByClass<ULSShellReactiveComponent>();
	if (!OtherReactive || OtherReactive->GetPhysicalBody() != OtherBody) return;
	OtherReactive->Contacts.Remove(Body);
	OtherReactive->StructuralContacts.Remove(Body);
}

void ULSShellReactiveComponent::ClearContactRelations()
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> OtherBodies;
	Contacts.GenerateKeyArray(OtherBodies);
	for (const auto& OtherBody : OtherBodies) RemoveContactRelation(OtherBody.Get());
	Contacts.Reset();
	StructuralContacts.Reset();
}

void ULSShellReactiveComponent::ReleaseSeparatedContacts()
{
	if (!Body.IsValid() || !Body->IsQueryCollisionEnabled())
	{
		ClearContactRelations();
		ClearCharge();
		return;
	}
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SeparatedBodies;
	for (const auto& Contact : Contacts)
	{
		UPrimitiveComponent* Other = Contact.Key.Get();
		// 접촉 허용 오차는 CharacterMovement의 지면 간격도 연속 접촉으로 보존한다.
		if (!IsValid(Other) || !Other->IsCollisionEnabled() ||
			(!StructuralContacts.Contains(Contact.Key) && !Other->OverlapComponent(
				Body->GetComponentLocation(), Body->GetComponentQuat(), Body->GetCollisionShape(3))))
			SeparatedBodies.Add(Contact.Key);
	}
	for (const auto& OtherBody : SeparatedBodies)
	{
		if (OtherBody.IsValid()) RemoveContactRelation(OtherBody.Get());
		else { Contacts.Remove(OtherBody); StructuralContacts.Remove(OtherBody); }
	}
}

void ULSShellReactiveComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	ReleaseSeparatedContacts();
	UpdateArenaState(Delta);
	ApplyArenaGather();
	PreResponseCenter = Body.IsValid() ? Body->GetCenterOfMass() : GetOwner()->GetActorLocation();
	PreResponseAngularVelocity = FVector::ZeroVector;
	if (Body.IsValid() && Body->IsSimulatingPhysics())
	{
		// Chaos의 V에는 아직 소비하지 않은 AddImpulse가 없으므로 다음 스텝의 입력을 합산한다.
		PreResponseVelocity = Body->GetPhysicsLinearVelocity() + PendingMomentumVelocity;
		PendingMomentumVelocity = ConsumedAfterSampleVelocity = FVector::ZeroVector;
		PreResponseAngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
		const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
		if (Coordinator && Coordinator->IsSupplyExperiment())
		{
			if (const auto* Settings = ALSBattlefieldSettings::Find(GetWorld()))
			{
				const FBox Box = Body->Bounds.GetBox();
				const FBox2D Bounds = Settings->GetWorldBounds();
				if (Box.Max.Z < -1000 || Box.Max.X < Bounds.Min.X - 200 || Box.Min.X > Bounds.Max.X + 200 ||
					Box.Max.Y < Bounds.Min.Y - 200 || Box.Min.Y > Bounds.Max.Y + 200)
				{
					UE_LOG(LogTemp, Display, TEXT("SHELL_BODY_EXIT Body=%s BodyId=%llu BoundsMin=%s BoundsMax=%s Velocity=%s"),
						*GetOwner()->GetName(), BodyId, *Box.Min.ToCompactString(), *Box.Max.ToCompactString(), *Body->GetPhysicsLinearVelocity().ToCompactString());
					GetOwner()->Destroy();
					return;
				}
			}
		}
		return;
	}
	if (!Character.IsValid()) return;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	PreResponseVelocity = Movement->PendingLaunchVelocity.IsNearlyZero() ? Movement->Velocity : Movement->PendingLaunchVelocity;
	if (!bForcedMotion) return;
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner())) return;
	if (LastImpulseFrame + 1 >= GFrameCounter || (bHasGravity && LastGravityFrame + 1 >= GFrameCounter) ||
		!Movement->PendingLaunchVelocity.IsNearlyZero() || !Movement->IsMovingOnGround() ||
		Movement->Velocity.Size() > RecoverySpeed) return;
	bForcedMotion = false;
	bHasGravity = false;
	Movement->SetAvoidanceEnabled(bAvoidanceWasEnabled);
	if (Behavior.IsValid() && bBehaviorWasTicking) Behavior->SetComponentTickEnabled(true);
}

void ULSShellReactiveComponent::OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!IsValid(OtherActor) || OtherActor == GetOwner() || !IsValid(OtherComponent) || HitComponent != Body.Get()) return;
	ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	ULSShellReactiveComponent* OtherReactive = OtherActor->FindComponentByClass<ULSShellReactiveComponent>();
	const bool bSupply = Coordinator && Coordinator->IsSupplyExperiment();
	if (bSupply && OtherReactive && OtherReactive->GetPhysicalBody() != OtherComponent) OtherReactive = nullptr;
	if (!Coordinator || !Coordinator->IsBattleActive() ||
		(!bSupply && !IsForcedMotionActive() && (!OtherReactive || !OtherReactive->IsForcedMotionActive())) ||
		Contacts.Contains(OtherComponent)) return;
	uint64 ContactEpoch = OtherReactive ? OtherReactive->GetContinuingContactEpoch(Body.Get()) : 0;
	if (ContactEpoch)
	{
		Contacts.Add(OtherComponent, ContactEpoch);
		return;
	}
	ContactEpoch = AllocateContactEpoch();
	Contacts.Add(OtherComponent, ContactEpoch);
	// 양쪽의 Hit 알림이 같은 접촉을 큐에 두 번 넣지 않도록 함께 잠근다.
	if (OtherReactive)
	{
		OtherReactive->Contacts.Add(Body, ContactEpoch);
	}
	const FVector OtherVelocity = OtherReactive ? (bSupply ? OtherReactive->GetSampledSurfaceVelocity(Hit.ImpactPoint) : OtherReactive->GetPreResponseVelocity()) : OtherActor->GetVelocity();
	if (bSupply)
		UE_LOG(LogTemp, Display, TEXT("SHELL_SUPPLY_HIT Moving=%s Other=%s SampleVn=%.3f SolverImpulse=%s"),
			*GetOwner()->GetName(), *OtherActor->GetName(),
			FMath::Abs(FVector::DotProduct(GetSampledSurfaceVelocity(Hit.ImpactPoint) - OtherVelocity, Hit.ImpactNormal)),
			*NormalImpulse.ToCompactString());
	Coordinator->QueueContact(GetOwner(), OtherActor, Hit.ImpactPoint, Hit.ImpactNormal,
		bSupply ? GetSampledSurfaceVelocity(Hit.ImpactPoint) : PreResponseVelocity, OtherVelocity, ContactEpoch);
}

void ULSShellReactiveComponent::OnLanded(const FHitResult& Hit)
{
	OnBodyHit(Body.Get(), Hit.GetActor(), Hit.GetComponent(), FVector::ZeroVector, Hit);
}

bool ULSShellReactiveComponent::RequestDetonation()
{
	return RequestDetonationWithCause(ELSShellDetonationTrigger::Damage, GetOwner()->GetActorLocation());
}

bool ULSShellReactiveComponent::RequestDetonationWithCause(ELSShellDetonationTrigger Trigger, FVector Origin)
{
	ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!bExplosive || bDetonationQueued || !Coordinator || !Coordinator->IsBattleActive()) return false;
	bDetonationQueued = true;
	FLSShellExplosion Explosion;
	Coordinator->CaptureShotContext(Explosion.Shot, GetOwner());
	Explosion.EmitterBody = Body;
	Explosion.Location = GetOwner()->GetActorLocation();
	Explosion.Damage = DetonationDamage;
	Explosion.Radius = DetonationRadius;
	Explosion.Velocity = DetonationVelocity;
	Explosion.Shot.Source = GetOwner();
	Explosion.Shot.Origin = Explosion.Location;
	Explosion.Trigger = Trigger;
	Explosion.TriggerOrigin = Origin;
	Explosion.bHasTriggerOrigin = !Origin.ContainsNaN();
	if (const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(GetOwner())) Explosion.Shot.Faction = Faction->GetFaction();
	Coordinator->QueueExplosion(Explosion);
	return true;
}

void ULSShellReactiveComponent::OnDied(const FLSDamageResult& Damage)
{
	RequestDetonationWithCause(DamageFeedbackTrigger,
		Damage.bHasAttackOrigin ? Damage.AttackOrigin : GetOwner()->GetActorLocation());
}
