#include "ShellExperiment/LSShellMaterialStateComponent.h"

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellTransport.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Chaos/ChaosEngineInterface.h"

namespace
{
UStaticMeshComponent* CreateMaterialVisual(AActor* Owner, UPrimitiveComponent* Parent,
	const TCHAR* MeshPath, FLinearColor Color, float Opacity)
{
	auto* Mesh = NewObject<UStaticMeshComponent>(Owner);
	Owner->AddInstanceComponent(Mesh);
	Mesh->SetupAttachment(Parent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellEffect.M_ShellEffect"));
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (Base)
	{
		auto* Material = UMaterialInstanceDynamic::Create(Base, Owner);
		Material->SetVectorParameterValue(TEXT("Tint"), Color);
		Material->SetVectorParameterValue(TEXT("TeamColor"), Color);
		Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mesh->SetMaterial(0, Material);
	}
	Mesh->RegisterComponent();
	return Mesh;
}

void DestroyMaterialVisual(UStaticMeshComponent* Mesh)
{
	if (!IsValid(Mesh)) return;
	if (AActor* Owner = Mesh->GetOwner()) Owner->RemoveInstanceComponent(Mesh);
	Mesh->DestroyComponent();
}
}

ULSShellMaterialStateComponent::ULSShellMaterialStateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

ULSShellMaterialStateComponent* ULSShellMaterialStateComponent::GetOrCreate(AActor* Actor)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return nullptr;
	if (auto* Existing = Actor->FindComponentByClass<ULSShellMaterialStateComponent>()) return Existing;
	auto* Result = NewObject<ULSShellMaterialStateComponent>(Actor);
	Actor->AddInstanceComponent(Result);
	Result->RegisterComponent();
	return Result;
}

void ULSShellMaterialStateComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
}

ULSShellReactiveComponent* ULSShellMaterialStateComponent::GetReactive() const
{
	return GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
}

bool ULSShellMaterialStateComponent::IsCooled() const
{
	return bPermanentIce || (GetWorld() && ColdUntil > GetWorld()->GetTimeSeconds());
}

float ULSShellMaterialStateComponent::GetColdSecondsRemaining() const
{
	if (bPermanentIce) return -1;
	return GetWorld() ? FMath::Max(0.f, float(ColdUntil - GetWorld()->GetTimeSeconds())) : 0;
}

int32 ULSShellMaterialStateComponent::GetActiveBondCount() const
{
	int32 Count = 0;
	for (const auto& Bond : Bonds)
		if (Bond.Other.IsValid() && Bond.Constraint.IsValid()) ++Count;
	for (const auto& Owner : BondOwners) if (Owner.IsValid()) ++Count;
	return Count;
}

bool ULSShellMaterialStateComponent::ShouldShatter(float NormalSpeed) const
{
	return IsCooled() && FMath::IsFinite(NormalSpeed) && NormalSpeed >= ShatterNormalSpeed;
}

FString ULSShellMaterialStateComponent::GetRecoveryDiagnostics() const
{
	const auto* Reactive = GetReactive();
	const UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	const auto* Movement = Character.IsValid() ? Character->GetCharacterMovement() : nullptr;
	return FString::Printf(TEXT("SHELL_THAW T=%.3f Rigid=%d Cold=%d Bonds=%d Active=%d Alive=%d Body=%d Sim=%d Collision=%d Ground=%d UpZ=%.5f Speed=%.3f AngularSpeed=%.5f Stable=%.3f Position=%s V=%s W=%s MoveMode=%d MoveTick=%d PendingLaunch=%s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1., bRigidCharacter, IsCooled(), GetActiveBondCount(),
		Coordinator && Coordinator->IsBattleActive(), ULSCombatTargetLibrary::IsCombatAlive(GetOwner()), Body != nullptr,
		Body && Body->IsSimulatingPhysics(), Body ? int32(Body->GetCollisionEnabled()) : -1, IsGrounded(),
		Body ? Body->GetUpVector().Z : 0., Body ? Body->GetPhysicsLinearVelocity().Size() : -1.,
		Body ? Body->GetPhysicsAngularVelocityInRadians().Size() : -1., StableSeconds,
		Body ? *Body->GetComponentLocation().ToCompactString() : TEXT("none"),
		Body ? *Body->GetPhysicsLinearVelocity().ToCompactString() : TEXT("none"),
		Body ? *Body->GetPhysicsAngularVelocityInRadians().ToCompactString() : TEXT("none"),
		Movement ? int32(Movement->MovementMode) : -1, Movement && Movement->IsComponentTickEnabled(),
		Movement ? *Movement->PendingLaunchVelocity.ToCompactString() : TEXT("none"));
}

int32 ULSShellMaterialStateComponent::ApplyAdhesion(int32 BondSlots, float StrengthScale)
{
	ULSShellReactiveComponent* Reactive = GetReactive();
	if (!Reactive || !Reactive->IsPhysicalBodyPresent() || BondSlots <= 0 ||
		!FMath::IsFinite(StrengthScale) || StrengthScale <= 0) return 0;
	const int32 Accepted = FMath::Clamp(BondSlots, 0, FMath::Max(0, 3 - GetActiveBondCount() - AvailableBondStrengths.Num()));
	for (int32 Index = 0; Index < Accepted; ++Index) AvailableBondStrengths.Add(BondLinearBreakImpulse * StrengthScale);
	RefreshBonds();
	return Accepted;
}

bool ULSShellMaterialStateComponent::ApplyCooling(float DurationSeconds)
{
	ULSShellReactiveComponent* Reactive = GetReactive();
	if (!GetWorld() || !Reactive || !Reactive->IsPhysicalBodyPresent() ||
		!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0 || !EnsureRigidCharacter()) return false;
	ColdUntil = FMath::Max(ColdUntil, double(GetWorld()->GetTimeSeconds()) + DurationSeconds);
	UpdateColdMaterial(true);
	UpdatePresentation();
	SetControlReason(TEXT("Cooling"), true);
	StableSeconds = 0;
	return true;
}

bool ULSShellMaterialStateComponent::EnsureRigidCharacter()
{
	if (!Character.IsValid()) Character = Cast<ACharacter>(GetOwner());
	if (!Character.IsValid() || bRigidCharacter) return true;
	if (!ULSCombatTargetLibrary::IsCombatAlive(GetOwner())) return false;
	auto* Reactive = GetReactive();
	UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!Body) return false;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector Velocity = Movement->PendingLaunchVelocity.IsNearlyZero() ? Movement->Velocity : Movement->PendingLaunchVelocity;
	PreviousMovementMode = Movement->MovementMode;
	PreviousCustomMovementMode = Movement->CustomMovementMode;
	bMovementWasTicking = Movement->IsComponentTickEnabled();
	bMovementAvoidance = Movement->bUseRVOAvoidance;
	Behavior = GetOwner()->FindComponentByClass<ULSEnemyBehaviorComponent>();
	Attack = GetOwner()->FindComponentByClass<ULSBasicAttackComponent>();
	if (Behavior.IsValid())
	{
		bBehaviorWasTicking = Behavior->IsComponentTickEnabled();
		Behavior->SetComponentTickEnabled(false);
	}
	if (Attack.IsValid())
	{
		bAttackWasTicking = Attack->IsComponentTickEnabled();
		Attack->StopAttack();
		Attack->SetComponentTickEnabled(false);
	}
	if (AController* Controller = Character->GetController()) Controller->StopMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	Movement->SetAvoidanceEnabled(false);
	Movement->SetComponentTickEnabled(false);
	Body->SetMassOverrideInKg(NAME_None, Movement->Mass, true);
	PreviousCollisionMode = Body->GetCollisionEnabled();
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetSimulatePhysics(true);
	if (!Body->IsSimulatingPhysics())
	{
		Body->SetCollisionEnabled(PreviousCollisionMode);
		Movement->SetMovementMode(EMovementMode(PreviousMovementMode), PreviousCustomMovementMode);
		Movement->SetComponentTickEnabled(bMovementWasTicking);
		Movement->SetAvoidanceEnabled(bMovementAvoidance);
		if (Behavior.IsValid()) Behavior->SetComponentTickEnabled(bBehaviorWasTicking);
		if (Attack.IsValid()) Attack->SetComponentTickEnabled(bAttackWasTicking);
		return false;
	}
	Body->SetPhysicsLinearVelocity(Velocity);
	bRigidCharacter = true;
	return true;
}

void ULSShellMaterialStateComponent::UpdateColdMaterial(bool bCold)
{
	ULSShellReactiveComponent* Reactive = GetReactive();
	UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!Body) return;
	if (bCold == bColdMaterialApplied)
	{
		if (bCold && ColdMaterial && Body->BodyInstance.GetSimplePhysicalMaterial() != ColdMaterial)
			Body->SetPhysMaterialOverride(ColdMaterial);
		return;
	}
	if (bCold)
	{
		PreviousMaterial = Body->BodyInstance.GetSimplePhysicalMaterial();
		ColdMaterial = NewObject<UPhysicalMaterial>(this);
		ColdMaterial->Friction = ColdMaterial->StaticFriction = .05f;
		ColdMaterial->Restitution = .05f;
		ColdMaterial->FrictionCombineMode = ColdMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
		ColdMaterial->bOverrideFrictionCombineMode = ColdMaterial->bOverrideRestitutionCombineMode = true;
		FChaosEngineInterface::UpdateMaterial(ColdMaterial->GetPhysicsMaterial(), ColdMaterial);
		Body->SetPhysMaterialOverride(ColdMaterial);
	}
	else Body->SetPhysMaterialOverride(PreviousMaterial);
	bColdMaterialApplied = bCold;
}

void ULSShellMaterialStateComponent::SetControlReason(FName Reason, bool bHeld)
{
	const FName Key(*FString::Printf(TEXT("Material_%u_%s"), GetUniqueID(), *Reason.ToString()));
	auto& Previous = ControlOwners.FindOrAdd(Reason);
	auto& PreviousSieges = SiegeControlOwners.FindOrAdd(Reason);
	TArray<TWeakObjectPtr<ALSShellTransport>> Current;
	TArray<TWeakObjectPtr<ULSShellSiegeController>> CurrentSieges;
	if (bHeld)
	{
		TArray<ULSShellReactiveComponent*> Pending;
		TSet<ULSShellReactiveComponent*> Visited;
		if (auto* Reactive = GetReactive()) Pending.Add(Reactive);
		for (int32 Index = 0; Index < Pending.Num(); ++Index)
		{
			auto* Reactive = Pending[Index];
			if (!IsValid(Reactive) || Visited.Contains(Reactive)) continue;
			Visited.Add(Reactive);
			if (auto* Transport = Cast<ALSShellTransport>(Reactive->GetOwner())) Current.AddUnique(Transport);
			if (auto* Siege = Reactive->GetOwner()->FindComponentByClass<ULSShellSiegeController>()) CurrentSieges.AddUnique(Siege);
			for (auto* Contact : Reactive->GetStructuralContactBodies())
				if (IsValid(Contact) && IsValid(Contact->GetOwner()))
					Pending.Add(Contact->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>());
		}
	}
	for (const auto& Transport : Previous)
		if (Transport.IsValid() && !Current.Contains(Transport)) Transport->SetExternalControlHeld(Key, false);
	for (const auto& Transport : Current)
		if (Transport.IsValid()) Transport->SetExternalControlHeld(Key, true);
	for (const auto& Siege : PreviousSieges)
		if (Siege.IsValid() && !CurrentSieges.Contains(Siege)) Siege->SetExternalControlHeld(Key, false);
	for (const auto& Siege : CurrentSieges)
		if (Siege.IsValid()) Siege->SetExternalControlHeld(Key, true);
	Previous = MoveTemp(Current);
	PreviousSieges = MoveTemp(CurrentSieges);
}

bool ULSShellMaterialStateComponent::CreateBond(ULSShellMaterialStateComponent* Other, float MaximumImpulse, FVector ContactPoint)
{
	if (!IsValid(Other) || Other == this || GetActiveBondCount() >= 3 || Other->GetActiveBondCount() >= 3) return false;
	for (const auto& Bond : Bonds) if (Bond.Other == Other) return false;
	if (BondOwners.Contains(Other)) return false;
	ULSShellReactiveComponent* Reactive = GetReactive();
	ULSShellReactiveComponent* OtherReactive = Other->GetReactive();
	if (!Reactive || !OtherReactive || !Reactive->IsPhysicalBodyPresent() || !OtherReactive->IsPhysicalBodyPresent() ||
		!Reactive->CanReceiveMomentum() || !OtherReactive->CanReceiveMomentum()) return false;
	// 적재 결속 등 다른 소유자의 구조 접촉을 접착으로 덮거나 해제하지 않는다.
	if (Reactive->GetStructuralContactBodies().Contains(OtherReactive->GetPhysicalBody())) return false;
	if (!EnsureRigidCharacter() || !Other->EnsureRigidCharacter()) return false;
	UPrimitiveComponent* Body = Reactive->GetPhysicalBody();
	UPrimitiveComponent* OtherBody = OtherReactive->GetPhysicalBody();
	if (!Body->IsSimulatingPhysics() || !OtherBody->IsSimulatingPhysics()) return false;
	FVector SurfaceA, SurfaceB;
	if (Body->GetClosestPointOnCollision(OtherBody->GetComponentLocation(), SurfaceA) >= 0 &&
		OtherBody->GetClosestPointOnCollision(Body->GetComponentLocation(), SurfaceB) >= 0)
		ContactPoint = (SurfaceA + SurfaceB) * .5;
	auto* Constraint = NewObject<UPhysicsConstraintComponent>(GetOwner());
	GetOwner()->AddInstanceComponent(Constraint);
	Constraint->RegisterComponent();
	Constraint->SetWorldLocation(ContactPoint);
	Constraint->SetLinearXLimit(LCM_Locked, 0);
	Constraint->SetLinearYLimit(LCM_Locked, 0);
	Constraint->SetLinearZLimit(LCM_Locked, 0);
	Constraint->SetAngularSwing1Limit(ACM_Locked, 0);
	Constraint->SetAngularSwing2Limit(ACM_Locked, 0);
	Constraint->SetAngularTwistLimit(ACM_Locked, 0);
	Constraint->SetDisableCollision(true);
	// native force 상한은 dt에 따라 다른 충격에서 끊어진다. 물리 뒤 실제 스텝 충격을 직접 비교한다.
	Constraint->SetLinearBreakable(false, 0);
	Constraint->SetConstrainedComponents(Body, NAME_None, OtherBody, NAME_None);
	if (!Constraint->ConstraintInstance.IsValidConstraintInstance()) { Constraint->DestroyComponent(); return false; }
	FLSShellAdhesiveBond Bond;
	Bond.Other = Other; Bond.Constraint = Constraint; Bond.MaximumJointImpulse = MaximumImpulse;
	Bond.LocalAnchor = Body->GetComponentTransform().InverseTransformPosition(ContactPoint);
	Bond.OtherLocalAnchor = OtherBody->GetComponentTransform().InverseTransformPosition(ContactPoint);
	Bond.LocalContactNormal = Body->GetComponentTransform().InverseTransformVectorNoScale(
		(OtherBody->GetComponentLocation() - Body->GetComponentLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
	Bond.LinkVisual = CreateMaterialVisual(GetOwner(), Body, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(.15f, 1, .2f), .8f);
	Bond.BandVisual = CreateMaterialVisual(GetOwner(), Body, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(.15f, 1, .2f), .6f);
	UpdateBondPresentation(Bond);
	Bonds.Add(Bond);
	Other->BondOwners.Add(this);
	Reactive->SetStructuralContact(OtherReactive, false);
	SetControlReason(TEXT("Adhesion"), true);
	Other->SetControlReason(TEXT("Adhesion"), true);
	return true;
}

void ULSShellMaterialStateComponent::RemoveOwnedBond(int32 Index)
{
	const FLSShellAdhesiveBond Bond = Bonds[Index];
	Bonds.RemoveAt(Index);
	DestroyMaterialVisual(Bond.LinkVisual.Get());
	DestroyMaterialVisual(Bond.BandVisual.Get());
	if (Bond.Constraint.IsValid())
	{
		Bond.Constraint->BreakConstraint();
		Bond.Constraint->DestroyComponent();
	}
	if (auto* Other = Bond.Other.Get())
	{
		Other->BondOwners.Remove(this);
		if (auto* Reactive = GetReactive()) Reactive->ReleaseStructuralContact(Other->GetReactive());
		Other->SetControlReason(TEXT("Adhesion"), Other->GetActiveBondCount() > 0);
	}
	SetControlReason(TEXT("Adhesion"), GetActiveBondCount() > 0);
}

void ULSShellMaterialStateComponent::ReleaseBondWith(ULSShellMaterialStateComponent* Other)
{
	for (int32 Index = Bonds.Num() - 1; Index >= 0; --Index)
		if (Bonds[Index].Other == Other) RemoveOwnedBond(Index);
}

void ULSShellMaterialStateComponent::ReleaseAllBonds()
{
	while (!Bonds.IsEmpty()) RemoveOwnedBond(Bonds.Num() - 1);
	const auto Owners = BondOwners.Array();
	for (const auto& Owner : Owners) if (Owner.IsValid()) Owner->ReleaseBondWith(this);
	BondOwners.Reset();
	SetControlReason(TEXT("Adhesion"), false);
}

void ULSShellMaterialStateComponent::RefreshBonds(bool bMayCreateNew)
{
	for (int32 Index = Bonds.Num() - 1; Index >= 0; --Index)
	{
		auto& Bond = Bonds[Index];
		auto* Other = Bond.Other.Get();
		auto* Reactive = GetReactive();
		auto* OtherReactive = Other ? Other->GetReactive() : nullptr;
		if (!Reactive || !Reactive->IsPhysicalBodyPresent() || !OtherReactive || !OtherReactive->IsPhysicalBodyPresent() ||
			!Bond.Constraint.IsValid() || Bond.Constraint->ConstraintInstance.IsBroken())
		{
			RemoveOwnedBond(Index);
			continue;
		}
		FVector LinearImpulse, AngularImpulse;
		// Chaos의 GetConstraintForce는 이름과 달리 positional impulse / dt, 즉 kg·cm/s를 반환한다.
		Bond.Constraint->GetConstraintForce(LinearImpulse, AngularImpulse);
		const float Magnitude = LinearImpulse.Size();
		PeakBondImpulse = FMath::Max(PeakBondImpulse, Magnitude);
		if (Magnitude > Bond.MaximumJointImpulse) RemoveOwnedBond(Index);
	}
	if (!bMayCreateNew || AvailableBondStrengths.IsEmpty()) return;
	auto* Reactive = GetReactive();
	if (!Reactive || !Reactive->IsPhysicalBodyPresent()) return;
	for (auto* OtherBody : Reactive->GetContactBodies())
	{
		if (AvailableBondStrengths.IsEmpty()) break;
		if (!IsValid(OtherBody) || OtherBody->GetCollisionObjectType() == LSCollisionChannels::Ground ||
			OtherBody->GetCollisionObjectType() == ECC_WorldStatic || !IsValid(OtherBody->GetOwner())) continue;
		auto* OtherReactive = OtherBody->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
		if (!OtherReactive || OtherReactive->GetPhysicalBody() != OtherBody) continue;
		auto* Other = GetOrCreate(OtherBody->GetOwner());
		FVector ContactPoint = (Reactive->GetPhysicalBody()->GetComponentLocation() + OtherBody->GetComponentLocation()) * .5;
		if (CreateBond(Other, AvailableBondStrengths[0], ContactPoint)) AvailableBondStrengths.RemoveAt(0);
	}
}

bool ULSShellMaterialStateComponent::HasRigidControlReason() const { return IsCooled() || GetActiveBondCount() > 0; }

void ULSShellMaterialStateComponent::UpdateBondPresentation(FLSShellAdhesiveBond& Bond)
{
	auto* Reactive = GetReactive();
	auto* Other = Bond.Other.Get();
	auto* OtherReactive = Other ? Other->GetReactive() : nullptr;
	UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	UPrimitiveComponent* OtherBody = OtherReactive ? OtherReactive->GetPhysicalBody() : nullptr;
	if (!Body || !OtherBody) return;
	const FVector A = Body->GetComponentTransform().TransformPosition(Bond.LocalAnchor);
	const FVector B = OtherBody->GetComponentTransform().TransformPosition(Bond.OtherLocalAnchor);
	const FVector Offset = B - A;
	if (Bond.LinkVisual.IsValid())
	{
		// 큰 구체나 폭발 영역처럼 보이지 않도록 실제 두 anchor 사이의 짧은 선만 표시한다.
		Bond.LinkVisual->SetVisibility(Offset.Size() <= 120);
		Bond.LinkVisual->SetWorldLocation((A + B) * .5);
		Bond.LinkVisual->SetWorldRotation(FRotationMatrix::MakeFromZ(Offset.IsNearlyZero() ? FVector::UpVector : Offset).Rotator());
		Bond.LinkVisual->SetWorldScale3D(FVector(.045f, .045f, FMath::Max(2., Offset.Size()) / 100));
	}
	if (Bond.BandVisual.IsValid())
	{
		Bond.BandVisual->SetWorldLocation(A);
		Bond.BandVisual->SetWorldRotation(FRotationMatrix::MakeFromZ(
			Body->GetComponentTransform().TransformVectorNoScale(Bond.LocalContactNormal)).Rotator());
		Bond.BandVisual->SetWorldScale3D(FVector(.28f, .28f, .035f));
	}
}

void ULSShellMaterialStateComponent::UpdatePresentation()
{
	auto* Reactive = GetReactive();
	UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!IsCooled() || !Body || FrostBody.Get() != Body)
	{
		for (UStaticMeshComponent* Mesh : FrostVisuals) DestroyMaterialVisual(Mesh);
		FrostVisuals.Reset();
		FrostBody.Reset();
	}
	if (IsCooled() && Body && FrostVisuals.IsEmpty())
	{
		const auto AddShell = [this, Body](const TCHAR* Shape, FVector Location, FVector Scale)
		{
			auto* Mesh = CreateMaterialVisual(GetOwner(), Body, Shape, FLinearColor(.65f, .9f, 1), .22f);
			Mesh->SetRelativeLocation(Location);
			Mesh->SetRelativeScale3D(Scale * 1.025f);
			FrostVisuals.Add(Mesh);
		};
		if (const auto* Capsule = Cast<UCapsuleComponent>(Body))
		{
			const float Radius = Capsule->GetUnscaledCapsuleRadius();
			const float HalfCylinder = Capsule->GetUnscaledCapsuleHalfHeight() - Radius;
			if (HalfCylinder > 0) AddShell(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector::ZeroVector, FVector(Radius, Radius, HalfCylinder) / 50);
			AddShell(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(0, 0, HalfCylinder), FVector(Radius / 50));
			AddShell(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(0, 0, -HalfCylinder), FVector(Radius / 50));
		}
		else if (const auto* Sphere = Cast<USphereComponent>(Body))
			AddShell(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector::ZeroVector, FVector(Sphere->GetUnscaledSphereRadius() / 50));
		else if (const auto* Box = Cast<UBoxComponent>(Body))
			AddShell(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector::ZeroVector, Box->GetUnscaledBoxExtent() / 50);
		FrostBody = Body;
	}
	for (auto& Bond : Bonds) UpdateBondPresentation(Bond);
}

bool ULSShellMaterialStateComponent::IsGrounded() const
{
	const auto* Reactive = GetReactive();
	const UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!Body || !GetWorld()) return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShellMaterialRecoveryGround), false, GetOwner());
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByObjectType(Hit, Body->Bounds.Origin,
		Body->Bounds.Origin - FVector(0, 0, Body->Bounds.BoxExtent.Z + 8), Objects, Query) && Hit.ImpactNormal.Z >= .5f;
}

void ULSShellMaterialStateComponent::TryResumeCharacter(float DeltaSeconds)
{
	if (!bRigidCharacter || !Character.IsValid() || HasRigidControlReason()) { StableSeconds = 0; return; }
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner())) return;
	UPrimitiveComponent* Body = GetReactive()->GetPhysicalBody();
	if (!Body || !Body->IsSimulatingPhysics() || !IsGrounded()) { StableSeconds = 0; return; }
	const FVector Up = Body->GetUpVector();
	const FVector AngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
	FVector Axis = FVector::CrossProduct(Up, FVector::UpVector);
	// 완전히 뒤집힌 상태에서도 실제 토크를 줄 축을 정한다. 위치·회전은 직접 바꾸지 않는다.
	if (Axis.IsNearlyZero() && Up.Z < 0) Axis = Body->GetForwardVector();
	const FVector Acceleration = (Axis * RightingAngularGain - AngularVelocity * RightingDamping)
		.GetClampedToMaxSize(MaximumRightingAngularAcceleration);
	Body->AddTorqueInRadians(Acceleration, NAME_None, true);
	const bool bStable = Up.Z >= FMath::Cos(FMath::DegreesToRadians(ResumeMaximumTiltDegrees)) &&
		Body->GetPhysicsLinearVelocity().Size() < ResumeMaximumSpeed && AngularVelocity.Size() < .1f;
	StableSeconds = bStable ? StableSeconds + DeltaSeconds : 0;
	if (StableSeconds < ResumeStableSeconds) return;
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(PreviousCollisionMode);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->Velocity = Velocity;
	Movement->SetMovementMode(EMovementMode(PreviousMovementMode), PreviousCustomMovementMode);
	Movement->SetComponentTickEnabled(bMovementWasTicking);
	Movement->SetAvoidanceEnabled(bMovementAvoidance);
	if (Behavior.IsValid()) Behavior->SetComponentTickEnabled(bBehaviorWasTicking);
	if (Attack.IsValid()) Attack->SetComponentTickEnabled(bAttackWasTicking);
	bRigidCharacter = false;
	StableSeconds = 0;
}

void ULSShellMaterialStateComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, TickFunction);
	if (bTransferCommitted) return;
	if (const auto* Reactive = GetReactive(); Reactive && Reactive->IsPhysicalBodyPresent())
	{
		RecentPostPhysicsState = ReadCurrentBodyState();
		RecentPostPhysicsSequence = ++SnapshotSequence;
	}
	if (const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()); Coordinator && !Coordinator->IsBattleActive())
	{
		RefreshBonds(false);
		UpdatePresentation();
		return;
	}
	RefreshBonds();
	UpdateColdMaterial(IsCooled());
	UpdatePresentation();
	SetControlReason(TEXT("Cooling"), IsCooled());
	SetControlReason(TEXT("Adhesion"), GetActiveBondCount() > 0);
	TryResumeCharacter(DeltaSeconds);
}

FLSShellRigidBodyState ULSShellMaterialStateComponent::CaptureBodyState() const
{
	const ULSShellReactiveComponent* Reactive = GetReactive();
	if (Reactive && Reactive->IsPhysicalBodyPresent()) return ReadCurrentBodyState();
	if (PreparedDeathState.bValid && PreparedDeathSequence >= RecentPostPhysicsSequence) return PreparedDeathState;
	if (RecentPostPhysicsState.bValid) return RecentPostPhysicsState;
	return ReadCurrentBodyState();
}

FLSShellRigidBodyState ULSShellMaterialStateComponent::ReadCurrentBodyState() const
{
	FLSShellRigidBodyState State;
	const ULSShellReactiveComponent* Reactive = GetReactive();
	const UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!Body) return State;
	State.Transform = Body->GetComponentTransform();
	State.LinearVelocity = Reactive->GetMomentumAdjustedLinearVelocity();
	State.AngularVelocity = Body->IsSimulatingPhysics() ? Body->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;
	State.bHasAngularMomentum = Body->IsSimulatingPhysics();
	State.AngularMomentum = State.bHasAngularMomentum ? Body->ScaleByMomentOfInertia(State.AngularVelocity) : FVector::ZeroVector;
	State.MassKg = Reactive->GetPhysicsMassKg();
	State.bConductiveRelay = Reactive->bConductiveRelay;
	State.bCanStoreCharge = Reactive->bCanStoreCharge;
	State.bHasSurvivingBonds = GetActiveBondCount() > 0;
	State.bThrustPropelled = Reactive->IsThrustPropelled();
	State.OriginalCollisionOwner = GetOwner();
	if (const auto* Capsule = Cast<UCapsuleComponent>(Body))
	{
		State.Shape = ELSShellBodyShape::Capsule;
		State.ShapeExtent = FVector(Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight());
	}
	else if (const auto* Sphere = Cast<USphereComponent>(Body))
	{
		State.Shape = ELSShellBodyShape::Sphere;
		State.ShapeExtent = FVector(Sphere->GetUnscaledSphereRadius());
	}
	else if (const auto* Box = Cast<UBoxComponent>(Body))
	{
		State.Shape = ELSShellBodyShape::Box;
		State.ShapeExtent = Box->GetUnscaledBoxExtent();
	}
	else return State;
	State.bValid = true;
	return State;
}

void ULSShellMaterialStateComponent::CaptureBeforePhysicalDeath()
{
	const ULSShellReactiveComponent* Reactive = GetReactive();
	PreparedDeathState = Reactive && Reactive->IsPhysicalBodyPresent() ? ReadCurrentBodyState() : CaptureBodyState();
	PreparedDeathSequence = ++SnapshotSequence;
}

FLSShellMaterialReplacement ULSShellMaterialStateComponent::CreateIceReplacement(const FLSShellRigidBodyState& CapturedState)
{
	FLSShellMaterialReplacement Result;
	if (!IsCooled() || bTransferCommitted || !CapturedState.bValid || !GetWorld()) return Result;
	auto* Ice = GetWorld()->SpawnActorDeferred<ALSShellInertIce>(ALSShellInertIce::StaticClass(), CapturedState.Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ice) return Result;
	if (!Ice->InitializeBody(CapturedState)) { Ice->Destroy(); return Result; }
	Ice->FinishSpawning(CapturedState.Transform);
	if (!IsValid(Ice) || !Ice->GetPhysicalBody()->IsSimulatingPhysics()) { if (IsValid(Ice)) Ice->Destroy(); return Result; }
	Result.PreviousReactive = GetReactive(); Result.ReplacementReactive = Ice->Reactive;
	Result.Ice = Ice; Result.CapturedState = CapturedState;
	return Result;
}

TArray<FLSShellMaterialBondTransfer> ULSShellMaterialStateComponent::CaptureBondTransfers() const
{
	TArray<FLSShellMaterialBondTransfer> Transfers;
	const auto* Reactive = GetReactive();
	const UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : nullptr;
	if (!Body) return Transfers;
	for (const auto& Bond : Bonds)
		if (Bond.Other.IsValid() && Bond.Constraint.IsValid())
			Transfers.Add({Bond.Other, Body->GetComponentTransform().TransformPosition(Bond.LocalAnchor),
				Bond.MaximumJointImpulse});
	for (const auto& Owner : BondOwners)
		if (Owner.IsValid()) for (const auto& Bond : Owner->Bonds)
			if (Bond.Other == this && Bond.Constraint.IsValid())
				Transfers.Add({Owner, Body->GetComponentTransform().TransformPosition(Bond.OtherLocalAnchor),
					Bond.MaximumJointImpulse});
	return Transfers;
}

bool ULSShellMaterialStateComponent::AdoptBondTransfer(const FLSShellMaterialBondTransfer& Transfer)
{
	return CreateBond(Transfer.Other.Get(), Transfer.MaximumJointImpulse, Transfer.ContactPoint);
}

void ULSShellMaterialStateComponent::CommitMaterialTransfer(ULSShellMaterialStateComponent* Replacement)
{
	if (bTransferCommitted || !IsValid(Replacement) || Replacement == this || !Replacement->GetReactive()) return;
	Replacement->GetReactive()->RestoreThrustPropelled(CaptureBodyState().bThrustPropelled);
	Replacement->AvailableBondStrengths = MoveTemp(AvailableBondStrengths);
	const auto Transfers = CaptureBondTransfers();
	ReleaseAllBonds();
	for (UStaticMeshComponent* Mesh : FrostVisuals) DestroyMaterialVisual(Mesh);
	FrostVisuals.Reset();
	UPrimitiveComponent* ReplacementBody = Replacement->GetReactive()->GetPhysicalBody();
	for (const auto& Transfer : Transfers)
	{
		auto* Other = Transfer.Other.Get();
		UPrimitiveComponent* OtherBody = Other && Other->GetReactive() ? Other->GetReactive()->GetPhysicalBody() : nullptr;
		if (OtherBody && OtherBody->OverlapComponent(ReplacementBody->GetComponentLocation(), ReplacementBody->GetComponentQuat(),
			ReplacementBody->GetCollisionShape(3))) Replacement->AdoptBondTransfer(Transfer);
	}
	SetControlReason(TEXT("Cooling"), false);
	bTransferCommitted = true;
}

bool ULSShellMaterialStateComponent::PrepareSplitTransfer(const TArray<AActor*>& Children, FString& Error) const
{
	Error.Reset();
	if (bTransferCommitted || bPermanentIce || Children.Num() != 3 || !GetReactive() || !GetReactive()->IsPhysicalBodyPresent())
	{
		Error = TEXT("Material split needs one finite physical source and exactly three prepared children");
		return false;
	}
	TSet<const AActor*> Seen;
	for (AActor* Child : Children)
	{
		const auto* Material = IsValid(Child) ? Child->FindComponentByClass<ULSShellMaterialStateComponent>() : nullptr;
		const auto* Reactive = Material ? Material->GetReactive() : nullptr;
		if (!Material || Child == GetOwner() || Seen.Contains(Child) || Child->IsActorBeingDestroyed() ||
			!Reactive || !Reactive->IsPhysicalBodyPresent() || !Reactive->GetPhysicalBody()->IsSimulatingPhysics() ||
			!Material->ReadCurrentBodyState().bValid)
		{
			Error = TEXT("Material split child is missing, duplicated or lacks a prepared supported rigid body");
			return false;
		}
		Seen.Add(Child);
	}
	return true;
}

void ULSShellMaterialStateComponent::CommitSplitTransfer(const TArray<AActor*>& Children)
{
	FString Error;
	if (!PrepareSplitTransfer(Children, Error)) { UE_LOG(LogTemp, Error, TEXT("%s"), *Error); return; }
	TArray<ULSShellMaterialStateComponent*> Materials;
	for (AActor* Child : Children) Materials.Add(Child->FindComponentByClass<ULSShellMaterialStateComponent>());
	const float ColdShare = GetColdSecondsRemaining() / 3;
	for (auto* Material : Materials) if (ColdShare > 0) Material->ApplyCooling(ColdShare);
	for (int32 Index = 0; Index < AvailableBondStrengths.Num(); ++Index)
		Materials[Index % Materials.Num()]->AvailableBondStrengths.Add(AvailableBondStrengths[Index]);
	AvailableBondStrengths.Reset();
	const auto Transfers = CaptureBondTransfers();
	ReleaseAllBonds();
	for (const auto& Transfer : Transfers)
	{
		auto* Other = Transfer.Other.Get();
		UPrimitiveComponent* OtherBody = Other && Other->GetReactive() ? Other->GetReactive()->GetPhysicalBody() : nullptr;
		if (!OtherBody) continue;
		ULSShellMaterialStateComponent* Nearest = nullptr;
		double NearestSquaredDistance = TNumericLimits<double>::Max();
		for (auto* Material : Materials)
		{
			UPrimitiveComponent* Body = Material->GetReactive()->GetPhysicalBody();
			FVector SurfacePoint;
			const float RegionDistance = Body->GetClosestPointOnCollision(Transfer.ContactPoint, SurfacePoint);
			const double SquaredDistance = FVector::DistSquared(Body->GetComponentLocation(), Transfer.ContactPoint);
			if (Material->GetActiveBondCount() < 3 && RegionDistance >= 0 && RegionDistance <= 3 &&
				SquaredDistance < NearestSquaredDistance && OtherBody->OverlapComponent(Body->GetComponentLocation(),
					Body->GetComponentQuat(), Body->GetCollisionShape(3)))
			{
				Nearest = Material;
				NearestSquaredDistance = SquaredDistance;
			}
		}
		if (Nearest) Nearest->AdoptBondTransfer(Transfer);
	}
	ColdUntil = 0;
	UpdateColdMaterial(false);
	UpdatePresentation();
	SetControlReason(TEXT("Cooling"), false);
	bTransferCommitted = true;
}

void ULSShellMaterialStateComponent::SetPermanentIce()
{
	bPermanentIce = true;
	if (HasBegunPlay()) UpdateColdMaterial(true);
}

void ULSShellMaterialStateComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseAllBonds();
	for (UStaticMeshComponent* Mesh : FrostVisuals) DestroyMaterialVisual(Mesh);
	FrostVisuals.Reset();
	SetControlReason(TEXT("Cooling"), false);
	if (!bTransferCommitted) UpdateColdMaterial(false);
	Super::EndPlay(Reason);
}
