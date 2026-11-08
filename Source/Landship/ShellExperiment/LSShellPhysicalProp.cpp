#include "ShellExperiment/LSShellPhysicalProp.h"

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellDetonationCue.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellNavigationBodyComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Chaos/ChaosEngineInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Vision/LSVisionParticipantComponent.h"

ALSShellPhysicalProp::ALSShellPhysicalProp()
{
	Reactive = CreateDefaultSubobject<ULSShellReactiveComponent>(TEXT("Reactive"));
	MaterialState = CreateDefaultSubobject<ULSShellMaterialStateComponent>(TEXT("MaterialState"));
	NavigationBody = CreateDefaultSubobject<ULSShellNavigationBodyComponent>(TEXT("NavigationBody"));
	BodyMaterial = CreateDefaultSubobject<UPhysicalMaterial>(TEXT("BodyMaterial"));
	Health->bDestroyOnDeath = true;
	Health->DestroyDelay = 0;
	Vision->bConcealedOutsideSight = false;
	Vision->bProvidesSight = false;
	Vision->bTriggersEnemyDiscovery = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	VisualMaterial = Material.Object;
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
}

bool ALSShellPhysicalProp::SetGeneratedHealthBeforeBeginPlay(float Value)
{
	if (HasActorBegunPlay() || !FMath::IsFinite(Value) || Value <= 0) return false;
	MaxHealth = Value;
	return true;
}

void ALSShellPhysicalProp::ConfigurePhysicalBody()
{
	UPrimitiveComponent* PhysicalBody = GetPhysicalBody();
	if (!PhysicalBody) return;
	LSShellConfigureMaterialBody(PhysicalBody, BodyMaterial, IntrinsicMassKg, Friction, Restitution, LinearDamping, AngularDamping);
	Reactive->SetPhysicalBody(PhysicalBody);
	NavigationBody->SetNavigationBody(PhysicalBody);
}

void ALSShellPhysicalProp::UpdatePhysicalMaterial()
{
	UPrimitiveComponent* PhysicalBody = GetPhysicalBody();
	if (!PhysicalBody) return;
	PhysicalBody->SetLinearDamping(LinearDamping);
	PhysicalBody->SetAngularDamping(AngularDamping);
	BodyMaterial->Friction = Friction;
	BodyMaterial->StaticFriction = Friction;
	BodyMaterial->Restitution = Restitution;
	BodyMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
	BodyMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	BodyMaterial->bOverrideFrictionCombineMode = true;
	BodyMaterial->bOverrideRestitutionCombineMode = true;
	// 생성 뒤 에디터 기본값을 바꿔도 기존 솔버 재질 핸들이 이전 값을 보유하지 않게 한다.
	FChaosEngineInterface::UpdateMaterial(BodyMaterial->GetPhysicsMaterial(), BodyMaterial);
	PhysicalBody->SetPhysMaterialOverride(BodyMaterial);
}

void ALSShellPhysicalProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshGeometry();
	ConfigurePhysicalBody();
}

void ALSShellPhysicalProp::RefreshGeometry()
{
	if (!BodyVisualMaterial && VisualMaterial)
		BodyVisualMaterial = UMaterialInstanceDynamic::Create(VisualMaterial, this);
	if (BodyVisualMaterial)
	{
		BodyVisualMaterial->SetVectorParameterValue(TEXT("TeamColor"), BodyColor);
		Visual->SetMaterial(0, BodyVisualMaterial);
	}
}

FString ALSShellPhysicalProp::GetConfigurationError() const
{
	const FString ParentError = Super::GetConfigurationError();
	if (!ParentError.IsEmpty()) return ParentError;
	if (!FMath::IsFinite(IntrinsicMassKg) || IntrinsicMassKg <= 0)
		return TEXT("IntrinsicMassKg must be finite and positive");
	if (!FMath::IsFinite(Friction) || Friction < 0 || !FMath::IsFinite(Restitution) || Restitution < 0 || Restitution > 1)
		return TEXT("Friction must be nonnegative and restitution must be between zero and one");
	if (!FMath::IsFinite(LinearDamping) || LinearDamping < 0 || !FMath::IsFinite(AngularDamping) || AngularDamping < 0)
		return TEXT("Physical damping must be finite and nonnegative");
	if (Health->bDestroyOnDeath == RetainsPhysicalBodyOnDeath() || Health->DestroyDelay != 0)
		return TEXT("Physical death removal must match the body's retention policy");
	if (!FMath::IsFinite(Reactive->MovementResistance) || Reactive->MovementResistance != 1)
		return TEXT("Rigid props require MovementResistance equal to one");
	return FString();
}

void ALSShellPhysicalProp::BeginPlay()
{
	RefreshGeometry();
	ConfigurePhysicalBody();
	Super::BeginPlay();
	// Reactive의 기존 기폭 예약 뒤에 전이한다. 같은 사망에서 환경 페이로드를 다시 만들지 않는다.
	Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnMaterialDied);
	const ALSShellExperimentGameMode* Mode = Cast<ALSShellExperimentGameMode>(GetWorld()->GetAuthGameMode());
	const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Mode || !Mode->IsConfigurationValid() || !Coordinator || !Coordinator->IsSupplyExperiment() ||
		Health->GetInitializationState() != ELSHealthInitializationState::Ready)
	{
		Reactive->SetComponentTickEnabled(false);
		MaterialState->SetComponentTickEnabled(false);
		Reactive->bExplosive = false;
		DisableObstacle();
		return;
	}
	NavigationBody->SetTrackingEnabled(true);
	if (!bDeferPhysicsActivation) ActivatePhysicalBody();
}

bool ALSShellPhysicalProp::ActivatePhysicalBody()
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSShellExperimentGameMode>() : nullptr;
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Mode || !Mode->IsConfigurationValid() || !Coordinator || !Coordinator->IsSupplyExperiment() ||
		Health->GetInitializationState() != ELSHealthInitializationState::Ready || !GetPhysicalBody()) return false;
	GetPhysicalBody()->SetSimulatePhysics(true);
	return GetPhysicalBody()->IsSimulatingPhysics();
}

bool ALSShellPhysicalProp::IsObstacleActive() const
{
	const UPrimitiveComponent* PhysicalBody = GetPhysicalBody();
	return PhysicalBody && PhysicalBody->IsQueryCollisionEnabled() && PhysicalBody->IsSimulatingPhysics();
}

void ALSShellPhysicalProp::PrepareForDeath()
{
	if (MaterialState->IsCooled()) MaterialState->CaptureBeforePhysicalDeath();
	if (RetainsPhysicalBodyOnDeath())
	{
		if (MaterialState->IsCooled()) MaterialState->SetPermanentIce();
		Vision->NotifyCombatDeath();
		return;
	}
	// 공통 사망 알림은 뒤에서 발생한다. Reactive가 기폭 수치를 복사할 때까지 컴포넌트를 보존한다.
	if (UPrimitiveComponent* PhysicalBody = GetPhysicalBody()) PhysicalBody->SetSimulatePhysics(false);
	Super::PrepareForDeath();
}

void ALSShellPhysicalProp::OnMaterialDied(const FLSDamageResult& Damage)
{
	if (auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld())) Coordinator->HandleMaterialDeath(this);
}

ALSShellMetalBall::ALSShellMetalBall()
{
	Reactive->bConductiveRelay = true;
	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	Body->InitSphereRadius(Radius);
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	MaxHealth = 600;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Visual->SetStaticMesh(Sphere.Object);
	Visual->SetRelativeScale3D(FVector(Radius / 50));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto* Mark = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SpinMark%d"), Index));
		Mark->SetupAttachment(Body);
		Mark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mark->SetCanEverAffectNavigation(false);
		Mark->SetCastShadow(false);
		Mark->SetStaticMesh(Sphere.Object);
		SpinMarks.Add(Mark);
	}
}

void ALSShellMetalBall::RefreshGeometry()
{
	Body->SetSphereRadius(Radius);
	Visual->SetRelativeScale3D(FVector(Radius / 50));
	Super::RefreshGeometry();
	if (!SpinMarkMaterial && VisualMaterial)
	{
		SpinMarkMaterial = UMaterialInstanceDynamic::Create(VisualMaterial, this);
		SpinMarkMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.02f, .025f, .035f));
	}
	for (int32 Index = 0; Index < SpinMarks.Num(); ++Index)
	{
		const FVector Normal = FVector::UpVector.RotateAngleAxis(90.f * Index, FVector::RightVector);
		SpinMarks[Index]->SetRelativeLocation(Normal * Radius * .98f);
		SpinMarks[Index]->SetRelativeRotation(FRotationMatrix::MakeFromZ(Normal).Rotator());
		SpinMarks[Index]->SetRelativeScale3D(FVector(.22f, .22f, .05f) * Radius / 50);
		if (SpinMarkMaterial) SpinMarks[Index]->SetMaterial(0, SpinMarkMaterial);
	}
}

void ALSShellMetalBall::PrepareForDeath()
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (Coordinator && Coordinator->IsBattleActive() && Coordinator->IsSupplyExperiment())
	{
		FLSShellCueSpec Cue;
		Cue.Location = GetActorLocation(); Cue.Radius = Body->GetScaledSphereRadius() * 2;
		Cue.Color = FLinearColor(.65f, .8f, 1); Cue.Strength = ShatterFeedback.Strength;
		Cue.Caption = TEXT("SHATTER"); Cue.Sound = Coordinator->ImpactSound;
		Cue.Style = ELSShellCueStyle::Shatter; Cue.Direction = FVector::UpVector;
		Cue.DurationSeconds = ShatterFeedback.DurationSeconds;
		Cue.FlashSeconds = ShatterFeedback.FlashSeconds;
		Cue.PitchMultiplier = ShatterFeedback.PitchMultiplier;
		auto* ShatterCue = GetWorld()->SpawnActor<ALSShellDetonationCue>(Cue.Location, FRotator::ZeroRotator);
		ShatterCue->Initialize(Cue);
	}
	Super::PrepareForDeath();
}

ALSShellBarrel::ALSShellBarrel()
{
	Reactive->bCanStoreCharge = false;
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(HalfExtent);
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	MaxHealth = 100;
	IntrinsicMassKg = 60;
	Friction = .4f;
	Restitution = .1f;
	LinearDamping = .05f;
	AngularDamping = .05f;
	BodyColor = FLinearColor(.8f, .13f, .025f);
	Reactive->bExplosive = true;
	Reactive->DetonationDamage = 250;
	Reactive->DetonationRadius = 900;
	Reactive->DetonationVelocity = 1400;
	Reactive->DetonationImpactThreshold = 1100;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Visual->SetStaticMesh(Cylinder.Object);
	UpperBand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperBand"));
	LowerBand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerBand"));
	for (UStaticMeshComponent* Band : {UpperBand.Get(), LowerBand.Get()})
	{
		Band->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Band->SetCanEverAffectNavigation(false);
		Band->SetupAttachment(Body);
		Band->SetStaticMesh(Cylinder.Object);
	}
	Visual->SetRelativeScale3D(HalfExtent / 50);
	UpperBand->SetRelativeLocation(FVector(0, 0, HalfExtent.Z * .75f));
	LowerBand->SetRelativeLocation(FVector(0, 0, -HalfExtent.Z * .75f));
	const FVector BandScale(HalfExtent.X / 50 * 1.06f, HalfExtent.Y / 50 * 1.06f, .08f);
	UpperBand->SetRelativeScale3D(BandScale);
	LowerBand->SetRelativeScale3D(BandScale);
}

void ALSShellBarrel::RefreshGeometry()
{
	Body->SetBoxExtent(HalfExtent);
	Visual->SetRelativeScale3D(HalfExtent / 50);
	UpperBand->SetRelativeLocation(FVector(0, 0, HalfExtent.Z * .75f));
	LowerBand->SetRelativeLocation(FVector(0, 0, -HalfExtent.Z * .75f));
	const FVector BandScale(HalfExtent.X / 50 * 1.06f, HalfExtent.Y / 50 * 1.06f, .08f);
	UpperBand->SetRelativeScale3D(BandScale);
	LowerBand->SetRelativeScale3D(BandScale);
	Super::RefreshGeometry();
	if (!BandVisualMaterial && VisualMaterial)
		BandVisualMaterial = UMaterialInstanceDynamic::Create(VisualMaterial, this);
	if (BandVisualMaterial)
	{
		BandVisualMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.035f, .045f, .06f));
		UpperBand->SetMaterial(0, BandVisualMaterial);
		LowerBand->SetMaterial(0, BandVisualMaterial);
	}
}
