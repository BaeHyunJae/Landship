#include "ShellExperiment/LSShellTransport.h"

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/ConstructorHelpers.h"

ALSShellTransport::ALSShellTransport()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bDeferPhysicsActivation = true;
	Health->bDestroyOnDeath = false;
	MaxHealth = 100;
	IntrinsicMassKg = 600;
	Friction = .05f; Restitution = 0; LinearDamping = .05f; AngularDamping = .5f;
	BodyColor = FLinearColor(.14f, .24f, .28f);
	Reactive->bConductiveRelay = true;
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(FVector(140, 90, 70));
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Visual->SetStaticMesh(Cube.Object);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		auto* Decoration = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Decoration%d"), Index));
		Decoration->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Decoration->SetCanEverAffectNavigation(false);
		Decoration->SetupAttachment(Body);
		Decoration->SetStaticMesh(Index < 4 ? Cylinder.Object : Cube.Object);
		if (Index < 4)
		{
			Decoration->SetRelativeLocation(FVector(Index < 2 ? -90 : 90, Index % 2 ? -92 : 92, -35));
			Decoration->SetRelativeRotation(FRotator(90, 0, 0));
			Decoration->SetRelativeScale3D(FVector(.55f, .55f, .12f));
		}
		else
		{
			Decoration->SetRelativeLocation(FVector(Index == 4 ? -70 : 70, 0, 71));
			Decoration->SetRelativeScale3D(FVector(.035f, 1.75f, .02f));
		}
		Decorations.Add(Decoration);
	}
	FLSShellCargoSlot Ball;
	Ball.CargoClass = ALSShellMetalBall::StaticClass();
	Ball.LocalTransform.SetLocation(FVector(0, -60, 140));
	Ball.MassOverrideKg = 750;
	CargoSlots.Add(Ball);
	FLSShellCargoSlot Barrel;
	Barrel.CargoClass = ALSShellBarrel::StaticClass();
	Barrel.LocalTransform.SetLocation(FVector(0, 70, 130));
	Barrel.MassOverrideKg = 60;
	Barrel.bInsulated = true;
	CargoSlots.Add(Barrel);
}

void ALSShellTransport::RefreshGeometry()
{
	Body->SetBoxExtent(FVector(140, 90, 70));
	Visual->SetRelativeScale3D(FVector(2.8f, 1.8f, 1.4f));
	Super::RefreshGeometry();
}

FString ALSShellTransport::GetConfigurationError() const
{
	const FString ParentError = Super::GetConfigurationError();
	if (!ParentError.IsEmpty()) return ParentError;
	if (!CargoConfigurationError.IsEmpty()) return CargoConfigurationError;
	if (!bDeferPhysicsActivation) return TEXT("Transport must defer simulation until cargo constraints and contact identities are ready");
	if (RouteStart.ContainsNaN() || RouteEnd.ContainsNaN() || FVector::DistSquaredXY(RouteStart, RouteEnd) <= UE_KINDA_SMALL_NUMBER)
		return TEXT("Transport requires a finite nonzero world route segment");
	const float PositiveValues[] = { ForwardSpeed, MaximumAcceleration, VelocityResponseSeconds,
		LookAheadDistance, LateralVelocityGain, MaximumLateralSpeed, BrakingDistance, YawAngleGain, YawRateGain,
		MaximumYawAcceleration, MechanicalSuspensionDeltaSpeed, MaximumDriveTiltDegrees, ResumeMaximumSpeed,
		ResumeStableSeconds, ArrivalDistance, DeliveryHeadingToleranceDegrees, DeliveryAngularSpeedToleranceDegrees };
	for (float Value : PositiveValues)
		if (!FMath::IsFinite(Value) || Value <= 0) return TEXT("Transport driver and cargo settings must be finite and positive");
	for (const FLSShellCargoSlot& Slot : CargoSlots)
		if (!Slot.CargoClass || Slot.CargoClass->HasAnyClassFlags(CLASS_Abstract) || Slot.LocalTransform.ContainsNaN() ||
			!Slot.LocalTransform.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) ||
			!FMath::IsFinite(Slot.MassOverrideKg) || Slot.MassOverrideKg < 0)
			return TEXT("Transport cargo slots require concrete classes, finite unit-scale transforms and nonnegative mass overrides");
	if (!FMath::IsFinite(WreckFriction) || WreckFriction < 0 || !FMath::IsFinite(WreckRestitution) ||
		WreckRestitution < 0 || WreckRestitution > 1 || !FMath::IsFinite(WreckLinearDamping) || WreckLinearDamping < 0 ||
		!FMath::IsFinite(WreckAngularDamping) || WreckAngularDamping < 0)
		return TEXT("Transport wreck material settings are invalid");
	if (!GetActorScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) ||
		FVector::DotProduct(GetActorUpVector(), FVector::UpVector) < 1 - UE_KINDA_SMALL_NUMBER)
		return TEXT("Transport must start upright at unit scale");
	return FString();
}

void ALSShellTransport::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteComponent(Reactive);
	if (Health->GetInitializationState() != ELSHealthInitializationState::Ready || !Body->IsQueryCollisionEnabled())
	{
		DriveState = ELSShellTransportDriveState::Invalid;
		return;
	}
	if (!CreateCargo())
	{
		CargoConfigurationError = TEXT("Transport cargo support, clearance, constraints or physics activation failed");
		DriveState = ELSShellTransportDriveState::Invalid;
		UE_LOG(LogTemp, Error, TEXT("%s: %s"), *GetName(), *CargoConfigurationError);
		return;
	}
	float Inertia;
	SampleAssembly(PreviousAssemblyVelocity, Inertia);
	ObservedExternalForceFrame = GetAssemblyExternalForceFrame();
	bHasVelocitySample = true;
}

bool ALSShellTransport::CreateCargo()
{
	for (const FLSShellCargoSlot& Slot : CargoSlots)
	{
		const FTransform Transform = Slot.LocalTransform * GetActorTransform();
		auto* Cargo = GetWorld()->SpawnActorDeferred<ALSShellPhysicalProp>(Slot.CargoClass, Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Cargo)
		{
			for (ALSShellPhysicalProp* Existing : CargoActors) if (IsValid(Existing)) Existing->Destroy();
			return false;
		}
		Cargo->bDeferPhysicsActivation = true;
		if (Slot.MassOverrideKg > 0) Cargo->IntrinsicMassKg = Slot.MassOverrideKg;
		Cargo->FinishSpawning(Transform);
		CargoActors.Add(Cargo);
		Cargo->GetHealthComponent()->OnDiedEvent.AddUniqueDynamic(this, &ThisClass::OnCargoDied);
	}
	if (!ValidateLoadedGeometry())
	{
		for (ALSShellPhysicalProp* Cargo : CargoActors) if (IsValid(Cargo)) Cargo->Destroy();
		return false;
	}
	for (int32 Index = 0; Index < CargoActors.Num(); ++Index)
	{
		ALSShellPhysicalProp* Cargo = CargoActors[Index];
		auto* Constraint = NewObject<UPhysicsConstraintComponent>(this, *FString::Printf(TEXT("CargoConstraint%d"), Index));
		Constraint->SetupAttachment(Body);
		Constraint->SetLinearXLimit(LCM_Locked, 0);
		Constraint->SetLinearYLimit(LCM_Locked, 0);
		Constraint->SetLinearZLimit(LCM_Locked, 0);
		Constraint->SetAngularSwing1Limit(ACM_Locked, 0);
		Constraint->SetAngularSwing2Limit(ACM_Locked, 0);
		Constraint->SetAngularTwistLimit(ACM_Locked, 0);
		Constraint->SetDisableCollision(true);
		Constraint->RegisterComponent();
		Constraint->SetWorldLocation(Cargo->GetPhysicalBody()->GetComponentLocation());
		Constraint->SetConstrainedComponents(Body, NAME_None, Cargo->GetPhysicalBody(), NAME_None);
		CargoConstraints.Add(Constraint);
		CargoRacked.Add(true);
		Reactive->SetStructuralContact(Cargo->Reactive, CargoSlots[Index].bInsulated);
	}
	bool bJointsReady = true;
	for (const UPhysicsConstraintComponent* Constraint : CargoConstraints)
		bJointsReady = Constraint->ConstraintInstance.IsValidConstraintInstance() && bJointsReady;
	// 전하 접촉과 결속 프레임을 먼저 만들고, 첫 물리 스텝 전에 모든 몸체를 함께 활성화한다.
	bool bActivated = bJointsReady && ActivatePhysicalBody();
	if (bActivated)
		for (ALSShellPhysicalProp* Cargo : CargoActors) bActivated = Cargo->ActivatePhysicalBody() && bActivated;
	if (!bActivated)
	{
		Body->SetSimulatePhysics(false);
		ReleaseCargo();
		for (ALSShellPhysicalProp* Cargo : CargoActors) if (IsValid(Cargo)) Cargo->Destroy();
	}
	return bActivated;
}

bool ALSShellTransport::ValidateLoadedGeometry() const
{
	const FVector DeckExtent = Body->GetScaledBoxExtent();
	TArray<UPrimitiveComponent*> Bodies { Body.Get() };
	for (int32 Index = 0; Index < CargoActors.Num(); ++Index)
	{
		const ALSShellPhysicalProp* Cargo = CargoActors[Index];
		if (!IsValid(Cargo) || Cargo->GetHealthComponent()->GetInitializationState() != ELSHealthInitializationState::Ready) return false;
		UPrimitiveComponent* CargoBody = Cargo->GetPhysicalBody();
		const FVector Center = Body->GetComponentTransform().InverseTransformPosition(CargoBody->GetComponentLocation());
		float SupportHeight;
		if (const USphereComponent* Sphere = Cast<USphereComponent>(CargoBody)) SupportHeight = Sphere->GetScaledSphereRadius();
		else if (const UBoxComponent* Box = Cast<UBoxComponent>(CargoBody))
		{
			if (FVector::DotProduct(Box->GetUpVector(), Body->GetUpVector()) < 1 - UE_KINDA_SMALL_NUMBER) return false;
			SupportHeight = Box->GetScaledBoxExtent().Z;
		}
		else return false;
		if (FMath::Abs(Center.Z - SupportHeight - DeckExtent.Z) > UE_KINDA_SMALL_NUMBER ||
			FMath::Abs(Center.X) > DeckExtent.X || FMath::Abs(Center.Y) > DeckExtent.Y) return false;
		for (int32 Previous = 0; Previous < Index; ++Previous)
			if (CargoActors[Previous]->GetPhysicalBody()->OverlapComponent(CargoBody->GetComponentLocation(),
				CargoBody->GetComponentQuat(), CargoBody->GetCollisionShape(-UE_KINDA_SMALL_NUMBER))) return false;
		Bodies.Add(CargoBody);
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShellTransportInitialClearance), false, this);
	for (const ALSShellPhysicalProp* Cargo : CargoActors) Params.AddIgnoredActor(Cargo);
	for (const UPrimitiveComponent* Component : Bodies)
	{
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Component->GetComponentLocation(), Component->GetComponentQuat(),
			FCollisionObjectQueryParams::AllObjects, Component->GetCollisionShape(-UE_KINDA_SMALL_NUMBER), Params);
		for (const FOverlapResult& Overlap : Overlaps)
			if (Overlap.Component.IsValid() && Component->GetCollisionResponseToChannel(Overlap.Component->GetCollisionObjectType()) == ECR_Block &&
				Overlap.Component->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) == ECR_Block)
				return false;
	}
	FHitResult Ground;
	const FVector Bottom = Body->GetComponentLocation() - FVector(0, 0, DeckExtent.Z);
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	return GetWorld()->LineTraceSingleByObjectType(Ground, Bottom + FVector(0, 0, 1), Bottom - FVector(0, 0, 1), GroundObjects, Params) &&
		FMath::Abs(Ground.ImpactPoint.Z - Bottom.Z) <= UE_KINDA_SMALL_NUMBER;
}

TArray<ALSShellPhysicalProp*> ALSShellTransport::GetCargoActors() const
{
	TArray<ALSShellPhysicalProp*> Result;
	for (ALSShellPhysicalProp* Cargo : CargoActors) if (IsValid(Cargo) && !Cargo->IsActorBeingDestroyed()) Result.Add(Cargo);
	return Result;
}

bool ALSShellTransport::ReleaseCargo()
{
	if (bCargoReleased) return false;
	bool bReleased = false;
	for (int32 Index = 0; Index < CargoConstraints.Num(); ++Index) bReleased = ReleaseCargoSlot(Index) || bReleased;
	bCargoReleased = true;
	return bReleased;
}

bool ALSShellTransport::HasRackedCargo() const
{
	for (int32 Index = 0; Index < CargoRacked.Num(); ++Index)
		if (CargoRacked[Index] && CargoActors.IsValidIndex(Index) && IsValid(CargoActors[Index]) && !CargoActors[Index]->IsActorBeingDestroyed()) return true;
	return false;
}

bool ALSShellTransport::ReleaseCargoSlot(int32 Index)
{
	if (!CargoRacked.IsValidIndex(Index) || !CargoRacked[Index]) return false;
	CargoRacked[Index] = false;
	CargoConstraints[Index]->SetDisableCollision(false);
	CargoConstraints[Index]->BreakConstraint();
	if (CargoActors.IsValidIndex(Index) && IsValid(CargoActors[Index])) Reactive->ReleaseStructuralContact(CargoActors[Index]->Reactive);
	bCargoReleased = !CargoRacked.Contains(true);
	return true;
}

void ALSShellTransport::UnloadDeliveredCargo()
{
	for (int32 Index = 0; Index < CargoRacked.Num(); ++Index)
		if (!CargoSlots[Index].bKeepRackedOnDelivery) ReleaseCargoSlot(Index);
	bHasDeliveredCargo = true;
	DriveState = HasRackedCargo() ? ELSShellTransportDriveState::Parked : ELSShellTransportDriveState::Arrived;
}

void ALSShellTransport::OnCargoDied(const FLSDamageResult& Damage)
{
	for (int32 Index = 0; Index < CargoActors.Num(); ++Index)
		if (CargoActors[Index].Get() == Damage.Receiver.Get()) ReleaseCargoSlot(Index);
}

void ALSShellTransport::PrepareForDeath()
{
	DriveState = ELSShellTransportDriveState::Wreck;
	ReleaseCargo();
	Friction = WreckFriction; Restitution = WreckRestitution;
	LinearDamping = WreckLinearDamping; AngularDamping = WreckAngularDamping;
	UpdatePhysicalMaterial();
	Super::PrepareForDeath();
}

void ALSShellTransport::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseCargo();
	for (ALSShellPhysicalProp* Cargo : CargoActors)
		if (IsValid(Cargo)) Cargo->GetHealthComponent()->OnDiedEvent.RemoveAll(this);
	Super::EndPlay(Reason);
}

bool ALSShellTransport::StartRoute(FVector Start, FVector End)
{
	if (DriveState == ELSShellTransportDriveState::Wreck || DriveState == ELSShellTransportDriveState::Arrived ||
		bHasDeliveredCargo || DriveState == ELSShellTransportDriveState::Invalid || Start.ContainsNaN() || End.ContainsNaN() ||
		FVector::DistSquaredXY(Start, End) <= UE_KINDA_SMALL_NUMBER) return false;
	RouteStart = Start; RouteEnd = End;
	bRouteStarted = true;
	return true;
}

bool ALSShellTransport::StartExitRoute(const TArray<FVector>& Waypoints)
{
	if (!bHasDeliveredCargo || !bCargoReleased || bExiting || DriveState == ELSShellTransportDriveState::Wreck ||
		DriveState == ELSShellTransportDriveState::Invalid || Waypoints.IsEmpty()) return false;
	FVector Previous = GetActorLocation();
	for (const FVector& Point : Waypoints)
	{
		if (Point.ContainsNaN() || FVector::DistSquaredXY(Previous, Point) <= UE_KINDA_SMALL_NUMBER) return false;
		Previous = Point;
	}
	ExitWaypoints = Waypoints;
	ExitWaypointIndex = 0;
	RouteStart = GetActorLocation();
	RouteEnd = ExitWaypoints[0];
	bRouteStarted = bExiting = true;
	DriveState = ELSShellTransportDriveState::Exiting;
	return true;
}

bool ALSShellTransport::SetDeliveryExitDirection(FVector Direction)
{
	if (bHasDeliveredCargo || bExiting || Direction.ContainsNaN() || Direction.IsNearlyZero(UE_KINDA_SMALL_NUMBER)) return false;
	Direction.Z = 0;
	if (!Direction.Normalize()) return false;
	DeliveryExitDirection = Direction;
	bAlignBeforeDelivery = true;
	return true;
}

FString ALSShellTransport::GetDriveStateText() const
{
	switch (DriveState)
	{
	case ELSShellTransportDriveState::Waiting: return TEXT("출발 대기");
	case ELSShellTransportDriveState::Driving: return TEXT("수송 중");
	case ELSShellTransportDriveState::Suspended: return TEXT("외력 회복 중");
	case ELSShellTransportDriveState::Arrived: return TEXT("화물 하역");
	case ELSShellTransportDriveState::Wreck: return TEXT("잔해");
	case ELSShellTransportDriveState::Invalid: return TEXT("수송차 구성 오류");
	case ELSShellTransportDriveState::Exiting: return TEXT("빈 수송차 퇴장");
	case ELSShellTransportDriveState::Parked: return TEXT("엄폐 화물 배치");
	case ELSShellTransportDriveState::Aligning: return TEXT("하역 방향 정렬");
	}
	return FString();
}

bool ALSShellTransport::IsGrounded() const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShellTransportGround), false, this);
	for (const ALSShellPhysicalProp* Cargo : CargoActors) if (IsValid(Cargo)) Params.AddIgnoredActor(Cargo);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	FHitResult Hit;
	const FVector Start = Body->GetComponentLocation();
	const FVector End = Body->GetComponentTransform().TransformPosition(FVector(0, 0, -Body->GetUnscaledBoxExtent().Z)) - FVector(0, 0, 5);
	return GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params) &&
		FVector::DotProduct(Hit.ImpactNormal, FVector::UpVector) >= FMath::Cos(FMath::DegreesToRadians(MaximumDriveTiltDegrees));
}

void ALSShellTransport::SampleAssembly(FVector& OutVelocity, float& OutYawInertia)
{
	const TArray<UPrimitiveComponent*> Bodies = GetConnectedPhysicalBodies();
	AssemblyMassKg = 0; AssemblyCenterOfMass = FVector::ZeroVector; OutVelocity = FVector::ZeroVector;
	for (const UPrimitiveComponent* Component : Bodies)
	{
		const float Mass = Component->GetMass();
		AssemblyMassKg += Mass;
		AssemblyCenterOfMass += Component->GetCenterOfMass() * Mass;
		OutVelocity += Component->GetPhysicsLinearVelocity() * Mass;
	}
	if (AssemblyMassKg <= 0) { OutYawInertia = 0; return; }
	AssemblyCenterOfMass /= AssemblyMassKg; OutVelocity /= AssemblyMassKg;
	OutYawInertia = 0;
	for (const UPrimitiveComponent* Component : Bodies)
	{
		const FVector LocalUp = Component->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector);
		const FVector LocalInertia = Component->GetInertiaTensor();
		const FVector Offset = Component->GetCenterOfMass() - AssemblyCenterOfMass;
		OutYawInertia += FVector::DotProduct(LocalUp * LocalUp, LocalInertia) + Component->GetMass() * Offset.SizeSquared2D();
	}
}

TArray<UPrimitiveComponent*> ALSShellTransport::GetConnectedPhysicalBodies() const
{
	TArray<UPrimitiveComponent*> Bodies { Body.Get() };
	TSet<UPrimitiveComponent*> Visited { Body.Get() };
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const ULSShellReactiveComponent* Member = Bodies[Index]->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
		if (!Member || Member->GetPhysicalBody() != Bodies[Index]) continue;
		for (UPrimitiveComponent* Connected : Member->GetStructuralContactBodies())
		{
			if (!IsValid(Connected) || Visited.Contains(Connected) || !IsValid(Connected->GetOwner()) ||
				Connected->GetOwner()->IsActorBeingDestroyed() || !Connected->IsSimulatingPhysics()) continue;
			Visited.Add(Connected);
			Bodies.Add(Connected);
		}
	}
	return Bodies;
}

uint64 ALSShellTransport::GetAssemblyExternalForceFrame() const
{
	uint64 Frame = 0;
	for (const UPrimitiveComponent* Connected : GetConnectedPhysicalBodies())
		if (const auto* Member = Connected->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>())
			Frame = FMath::Max(Frame, Member->GetLastExternalForceFrame());
	return Frame;
}

void ALSShellTransport::SetExternalControlHeld(FName Reason, bool bHeld)
{
	if (Reason.IsNone()) return;
	if (bHeld) ExternalControlHolds.Add(Reason);
	else ExternalControlHolds.Remove(Reason);
}

void ALSShellTransport::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DriveState == ELSShellTransportDriveState::Invalid || !Body->IsSimulatingPhysics()) return;
	for (int32 Index = 0; Index < CargoRacked.Num(); ++Index)
		if (CargoRacked[Index] && (!IsValid(CargoActors[Index]) || CargoActors[Index]->IsActorBeingDestroyed())) ReleaseCargoSlot(Index);
	FVector Velocity;
	float YawInertia;
	SampleAssembly(Velocity, YawInertia);
	const float StepDeltaSpeed = bHasVelocitySample ? FVector::Dist(Velocity, PreviousAssemblyVelocity) : 0;
	PreviousAssemblyVelocity = Velocity; bHasVelocitySample = true;
	if (!bCargoReleased)
		for (int32 Index = 0; Index < CargoActors.Num(); ++Index)
			if (const ALSShellPhysicalProp* Cargo = CargoActors[Index]; CargoRacked[Index] && IsValid(Cargo))
			{
				FVector Nearest;
				const float Distance = Body->GetClosestPointOnCollision(Cargo->GetPhysicalBody()->GetComponentLocation(), Nearest);
				if (Distance >= 0)
				{
					FVector CargoNearest;
					const float Gap = Cargo->GetPhysicalBody()->GetClosestPointOnCollision(Nearest, CargoNearest);
					PeakLatchedGapCm = FMath::Max(PeakLatchedGapCm, Gap);
				}
			}
	if (DriveState == ELSShellTransportDriveState::Wreck || DriveState == ELSShellTransportDriveState::Arrived || DriveState == ELSShellTransportDriveState::Parked) return;
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive())
	{
		DriveState = ELSShellTransportDriveState::Waiting;
		return;
	}
	if (!bRouteStarted && (!bAutoStartRoute || !StartRoute(RouteStart, RouteEnd))) return;
	const uint64 ExternalFrame = GetAssemblyExternalForceFrame();
	const bool bNewExternalForce = ExternalFrame != ObservedExternalForceFrame;
	ObservedExternalForceFrame = ExternalFrame;
	const bool bUpright = FVector::DotProduct(GetActorUpVector(), FVector::UpVector) >= FMath::Cos(FMath::DegreesToRadians(MaximumDriveTiltDegrees));
	const bool bGrounded = IsGrounded();
	if (bNewExternalForce || !ExternalControlHolds.IsEmpty() || StepDeltaSpeed >= MechanicalSuspensionDeltaSpeed || !bUpright || !bGrounded)
	{
		DriveState = ELSShellTransportDriveState::Suspended;
		StableSeconds = 0;
	}
	if (DriveState == ELSShellTransportDriveState::Suspended)
	{
		if (!bNewExternalForce && ExternalControlHolds.IsEmpty() && bUpright && bGrounded && Velocity.Size() < ResumeMaximumSpeed) StableSeconds += DeltaSeconds;
		else StableSeconds = 0;
		if (StableSeconds < ResumeStableSeconds) return;
	}
	DriveState = bAligningForDelivery ? ELSShellTransportDriveState::Aligning : bExiting ? ELSShellTransportDriveState::Exiting : ELSShellTransportDriveState::Driving;
	const FVector Route = (RouteEnd - RouteStart) * FVector(1, 1, 0);
	const float Length = Route.Size();
	const FVector Forward = Route / Length;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
	const FVector Position = Body->GetCenterOfMass();
	const float Along = FVector::DotProduct(Position - RouteStart, Forward);
	const float Remaining = Length - Along;
	const bool bAtArrivalPose = FVector::DistXY(Position, RouteEnd) <= ArrivalDistance && Velocity.Size() < ResumeMaximumSpeed;
	bool bCanFinishArrival = bAtArrivalPose;
	if (!bExiting && bAlignBeforeDelivery)
	{
		if (bAtArrivalPose) bAligningForDelivery = true;
		if (bAligningForDelivery)
		{
			DriveState = ELSShellTransportDriveState::Aligning;
			const float DeliveryYaw = FMath::RadiansToDegrees(FMath::Atan2(DeliveryExitDirection.Y, DeliveryExitDirection.X));
			bCanFinishArrival = bAtArrivalPose && FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(GetActorRotation().Yaw), DeliveryYaw)) <= DeliveryHeadingToleranceDegrees &&
				FMath::RadiansToDegrees(Body->GetPhysicsAngularVelocityInRadians().Size()) <= DeliveryAngularSpeedToleranceDegrees;
		}
	}
	if (bCanFinishArrival)
	{
		if (bExiting && ExitWaypoints.IsValidIndex(ExitWaypointIndex + 1))
		{
			RouteStart = GetActorLocation();
			RouteEnd = ExitWaypoints[++ExitWaypointIndex];
		}
		else
		{
			if (!bExiting) UnloadDeliveredCargo();
			bAligningForDelivery = false;
			bExiting = false;
			if (DriveState != ELSShellTransportDriveState::Parked) DriveState = ELSShellTransportDriveState::Arrived;
		}
		return;
	}
	const FVector Nearest = RouteStart + Forward * FMath::Clamp(Along, 0.f, Length);
	const FVector LookAhead = RouteStart + Forward * FMath::Min(Length, FMath::Clamp(Along, 0.f, Length) + LookAheadDistance);
	const float LateralOffset = FVector::DotProduct(Position - Nearest, Side);
	const float Speed = ForwardSpeed * FMath::Clamp(Remaining / BrakingDistance, -1.f, 1.f);
	const FVector DesiredVelocity = Forward * Speed + Side * FMath::Clamp(-LateralVelocityGain * LateralOffset, -MaximumLateralSpeed, MaximumLateralSpeed);
	const FVector PlanarVelocity = Velocity * FVector(1, 1, 0);
	const FVector Acceleration = ((DesiredVelocity - PlanarVelocity) / VelocityResponseSeconds).GetClampedToMaxSize(MaximumAcceleration);
	const FVector ResistanceDirection = PlanarVelocity.IsNearlyZero() ? DesiredVelocity.GetSafeNormal() : PlanarVelocity.GetSafeNormal();
	const float ResistanceAcceleration = Friction * FMath::Abs(GetWorld()->GetGravityZ());
	const FVector Force = (AssemblyMassKg * (Acceleration + ResistanceDirection * ResistanceAcceleration)).GetClampedToMaxSize(
		AssemblyMassKg * (MaximumAcceleration + ResistanceAcceleration));
	Body->AddForceAtLocation(Force, AssemblyCenterOfMass);
	FVector Heading = bAligningForDelivery ? DeliveryExitDirection : Remaining <= LookAheadDistance ? Forward : (LookAhead - Position) * FVector(1, 1, 0);
	if (Heading.IsNearlyZero()) Heading = Forward;
	const float DesiredYaw = FMath::Atan2(Heading.Y, Heading.X);
	const float CurrentYaw = FMath::DegreesToRadians(GetActorRotation().Yaw);
	const float AngleError = FMath::FindDeltaAngleRadians(CurrentYaw, DesiredYaw);
	const float YawAcceleration = FMath::Clamp(YawAngleGain * AngleError - YawRateGain * Body->GetPhysicsAngularVelocityInRadians().Z,
		-MaximumYawAcceleration, MaximumYawAcceleration);
	Body->AddTorqueInRadians(FVector(0, 0, YawInertia * YawAcceleration));
}
