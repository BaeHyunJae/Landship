#include "ShellExperiment/LSShellMaterials.h"

#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellNavigationBodyComponent.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Chaos/ChaosEngineInterface.h"
#include "UObject/ConstructorHelpers.h"

void LSShellConfigureMaterialBody(UPrimitiveComponent* Body, UPhysicalMaterial* Material,
	float MassKg, float Friction, float Restitution, float LinearDamping, float AngularDamping)
{
	if (!Body || !Material) return;
	Body->SetSimulatePhysics(false);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(LSCollisionChannels::Building);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	Body->SetCanEverAffectNavigation(false);
	Body->BodyInstance.bUseCCD = true;
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	Body->SetLinearDamping(LinearDamping);
	Body->SetAngularDamping(AngularDamping);
	Material->Friction = Material->StaticFriction = Friction;
	Material->Restitution = Restitution;
	Material->FrictionCombineMode = Material->RestitutionCombineMode = EFrictionCombineMode::Min;
	Material->bOverrideFrictionCombineMode = Material->bOverrideRestitutionCombineMode = true;
	FChaosEngineInterface::UpdateMaterial(Material->GetPhysicsMaterial(), Material);
	Body->SetPhysMaterialOverride(Material);
}

ALSShellInertMaterial::ALSShellInertMaterial()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("InitialRoot")));
	Reactive = CreateDefaultSubobject<ULSShellReactiveComponent>(TEXT("Reactive"));
	MaterialState = CreateDefaultSubobject<ULSShellMaterialStateComponent>(TEXT("MaterialState"));
	NavigationBody = CreateDefaultSubobject<ULSShellNavigationBodyComponent>(TEXT("NavigationBody"));
	BodyMaterial = CreateDefaultSubobject<UPhysicalMaterial>(TEXT("BodyMaterial"));
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(GetRootComponent());
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Cube.Object);
}

bool ALSShellInertMaterial::InitializeBody(const FLSShellRigidBodyState& State)
{
	if (Body || HasActorBegunPlay() || !State.bValid || !FMath::IsFinite(State.MassKg) || State.MassKg <= 0 ||
		State.Transform.ContainsNaN() || State.LinearVelocity.ContainsNaN() || State.AngularVelocity.ContainsNaN() ||
		(State.bHasAngularMomentum && State.AngularMomentum.ContainsNaN()) ||
		State.ShapeExtent.ContainsNaN() || State.ShapeExtent.GetMin() <= 0) return false;
	InitialState = State;
	if (State.Shape == ELSShellBodyShape::Capsule)
	{
		auto* Capsule = NewObject<UCapsuleComponent>(this, TEXT("Body"));
		Capsule->InitCapsuleSize(State.ShapeExtent.X, State.ShapeExtent.Z);
		Body = Capsule;
	}
	else if (State.Shape == ELSShellBodyShape::Sphere)
	{
		auto* Sphere = NewObject<USphereComponent>(this, TEXT("Body"));
		Sphere->InitSphereRadius(State.ShapeExtent.X);
		Body = Sphere;
	}
	else
	{
		auto* Box = NewObject<UBoxComponent>(this, TEXT("Body"));
		Box->InitBoxExtent(State.ShapeExtent);
		Body = Box;
	}
	AddInstanceComponent(Body);
	SetRootComponent(Body);
	Body->SetWorldTransform(State.Transform);
	NavigationBody->SetNavigationBody(Body);
	Body->RegisterComponent();
	Visual->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
	if (State.Shape == ELSShellBodyShape::Sphere ||
		(State.Shape == ELSShellBodyShape::Capsule && FMath::IsNearlyEqual(State.ShapeExtent.X, State.ShapeExtent.Z)))
	{
		Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	}
	Visual->SetRelativeScale3D(State.ShapeExtent / 50);
	LSShellConfigureMaterialBody(Body, BodyMaterial, State.MassKg, InertFriction, .05f, .05f, .1f);
	Reactive->bConductiveRelay = State.bConductiveRelay;
	Reactive->bCanStoreCharge = State.bCanStoreCharge;
	Reactive->SetPhysicalBody(Body);
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype")))
	{
		VisualMaterialInstance = UMaterialInstanceDynamic::Create(Material, this);
		VisualMaterialInstance->SetVectorParameterValue(TEXT("TeamColor"), MaterialColor);
		Visual->SetMaterial(0, VisualMaterialInstance);
	}
	return true;
}

UPrimitiveComponent* ALSShellInertMaterial::GetPhysicalBody() const { return Body; }

void ALSShellInertMaterial::BeginPlay()
{
	Super::BeginPlay();
	if (!Body || !InitialState.bValid) return;
	Body->SetSimulatePhysics(true);
	Body->SetPhysicsLinearVelocity(InitialState.LinearVelocity);
	FVector AngularVelocity = InitialState.AngularVelocity;
	if (InitialState.bHasAngularMomentum)
	{
		const FVector Inertia = Body->GetInertiaTensor();
		if (Inertia.GetMin() > UE_SMALL_NUMBER)
		{
			const FQuat Rotation = Body->GetComponentQuat();
			AngularVelocity = Rotation.RotateVector(Rotation.UnrotateVector(InitialState.AngularMomentum) / Inertia);
		}
	}
	Body->SetPhysicsAngularVelocityInRadians(AngularVelocity);
	Reactive->SetPhysicalBody(Body);
	NavigationBody->SetTrackingEnabled(true);
}

ALSShellInertIce::ALSShellInertIce()
{
	InertFriction = .05f;
	MaterialState->SetPermanentIce();
}

ALSShellSiegeCarriage::ALSShellSiegeCarriage()
{
	MaterialColor = FLinearColor(.13f, .16f, .2f);
}

bool ALSShellSiegeCarriage::InitializeBody(const FLSShellRigidBodyState& State)
{
	// 형상을 줄여 기존 접착 부위가 사라지는 전환은 하지 않는다.
	if (State.bHasSurvivingBonds) return Super::InitializeBody(State);
	if (CapturedExtentScale.ContainsNaN() || CapturedExtentScale.GetMin() <= 0 ||
		State.Transform.ContainsNaN() || State.ShapeExtent.ContainsNaN() || State.ShapeExtent.GetMin() <= 0 || !GetWorld()) return false;
	FLSShellRigidBodyState CarriageState = State;
	CarriageState.Shape = ELSShellBodyShape::Box;
	CarriageState.ShapeExtent = State.ShapeExtent * CapturedExtentScale;
	const FVector WorldExtent = CarriageState.ShapeExtent * State.Transform.GetScale3D().GetAbs();
	if (WorldExtent.GetMin() <= 0) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShellCarriageShapeClearance), false, this);
	Query.AddIgnoredActor(State.OriginalCollisionOwner.Get());
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, State.Transform.GetLocation(), State.Transform.GetRotation(),
		FCollisionObjectQueryParams::AllObjects, FCollisionShape::MakeBox(WorldExtent), Query);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const UPrimitiveComponent* Other = Overlap.GetComponent();
		// 새 포가는 Building 몸체로 모든 채널을 막는다. 원본 Unit 몸체의 응답을 대신 쓰지 않는다.
		if (Other && Other->IsPhysicsCollisionEnabled() && Other->GetCollisionResponseToChannel(LSCollisionChannels::Building) == ECR_Block)
			return Super::InitializeBody(State);
	}
	return Super::InitializeBody(CarriageState);
}

ALSShellPlate::ALSShellPlate()
{
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(HalfExtent);
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	MaxHealth = 300;
	IntrinsicMassKg = 250;
	Friction = .35f; Restitution = .05f; LinearDamping = .05f; AngularDamping = .1f;
	Health->bDestroyOnDeath = false;
	Reactive->bConductiveRelay = true;
	BodyColor = FLinearColor(.16f, .2f, .25f);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Cube.Object);
	Support = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("Support"));
	Support->SetupAttachment(Body);
}

void ALSShellPlate::RefreshGeometry()
{
	Body->SetBoxExtent(HalfExtent);
	Visual->SetRelativeScale3D(HalfExtent / 50);
	Super::RefreshGeometry();
}

void ALSShellPlate::BeginPlay()
{
	Super::BeginPlay();
	if (!bInitiallySupported || !Body->IsSimulatingPhysics()) return;
	Support->SetWorldTransform(Body->GetComponentTransform());
	Support->SetLinearXLimit(LCM_Locked, 0);
	Support->SetLinearYLimit(LCM_Locked, 0);
	Support->SetLinearZLimit(LCM_Locked, 0);
	Support->SetAngularSwing1Limit(ACM_Locked, 0);
	Support->SetAngularSwing2Limit(ACM_Locked, 0);
	Support->SetAngularTwistLimit(ACM_Locked, 0);
	Support->SetConstrainedComponents(Body, NAME_None, nullptr, NAME_None);
}

bool ALSShellPlate::IsSupported() const
{
	return Support && Support->ConstraintInstance.IsValidConstraintInstance() && !Support->ConstraintInstance.IsBroken();
}

void ALSShellPlate::PrepareForDeath()
{
	Support->BreakConstraint();
	Super::PrepareForDeath();
}

ALSShellBattery::ALSShellBattery()
{
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(HalfExtent);
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	MaxHealth = 120;
	IntrinsicMassKg = 100;
	Friction = .35f; Restitution = .05f; LinearDamping = .05f; AngularDamping = .1f;
	Health->bDestroyOnDeath = false;
	Reactive->bConductiveRelay = true;
	BodyColor = FLinearColor(.1f, .55f, .7f);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Cube.Object);
}

void ALSShellBattery::RefreshGeometry()
{
	Body->SetBoxExtent(HalfExtent);
	Visual->SetRelativeScale3D(HalfExtent / 50);
	Super::RefreshGeometry();
}

bool ALSShellBattery::CommitInitialCharge()
{
	if (bInitialChargeCommitted || Reactive->GetStoredCharge() < 360) return false;
	bInitialChargeCommitted = true;
	return true;
}
