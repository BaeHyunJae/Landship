#include "LSShellDetonationCue.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "LSShellReactiveComponent.h"
#include "LSShellExperimentCoordinator.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
void FitBodyShell(UStaticMeshComponent* Mesh, UPrimitiveComponent* Body)
{
	FVector Extent;
	const TCHAR* MeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	if (const UBoxComponent* Box = Cast<UBoxComponent>(Body))
	{
		Extent = Box->GetUnscaledBoxExtent();
		MeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	}
	else if (const USphereComponent* Sphere = Cast<USphereComponent>(Body))
		Extent = FVector(Sphere->GetUnscaledSphereRadius());
	else if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Body))
		Extent = FVector(Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleRadius(), Capsule->GetUnscaledCapsuleHalfHeight());
	else
		Extent = Body->Bounds.BoxExtent;
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	Mesh->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
	Mesh->SetRelativeLocation(FVector::ZeroVector);
	Mesh->SetRelativeRotation(FRotator::ZeroRotator);
	Mesh->SetRelativeScale3D((Extent + FVector(2)) / 50.f);
}

void FitCapsuleShell(UStaticMeshComponent* Middle, UStaticMeshComponent* EndA,
	UStaticMeshComponent* EndB, UCapsuleComponent* Capsule)
{
	const float Radius = Capsule->GetUnscaledCapsuleRadius();
	const float HalfCylinder = Capsule->GetUnscaledCapsuleHalfHeight() - Radius;
	Middle->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Middle->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
	Middle->SetRelativeLocation(FVector::ZeroVector);
	Middle->SetRelativeRotation(FRotator::ZeroRotator);
	Middle->SetRelativeScale3D(FVector(Radius + 2, Radius + 2, HalfCylinder) / 50.f);
	for (UStaticMeshComponent* End : {EndA, EndB})
	{
		End->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
		End->SetRelativeRotation(FRotator::ZeroRotator);
		End->SetRelativeScale3D(FVector((Radius + 2) / 50.f));
	}
	EndA->SetRelativeLocation(FVector(0, 0, HalfCylinder));
	EndB->SetRelativeLocation(FVector(0, 0, -HalfCylinder));
}

UMaterialInstanceDynamic* SetCueMaterial(UStaticMeshComponent* Mesh, UObject* Owner,
	FLinearColor Color, float Opacity, bool bRing = false)
{
	const TCHAR* Path = bRing
		? TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellRing.M_ShellRing")
		: TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellEffect.M_ShellEffect");
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!Base) return nullptr;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Owner);
	Material->SetVectorParameterValue(TEXT("Tint"), Color);
	Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	Mesh->SetMaterial(0, Material);
	return Material;
}
}

ALSShellDetonationCue::ALSShellDetonationCue()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Ignition = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ignition"));
	Flash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flash"));
	Shockwave = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shockwave"));
	Link = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CauseLink"));
	ThrustHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThrustHead"));
	CapsuleEndA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleEndA"));
	CapsuleEndB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleEndB"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	for (UStaticMeshComponent* Mesh : {Ignition.Get(), Flash.Get(), Shockwave.Get(), Link.Get(), ThrustHead.Get(), CapsuleEndA.Get(), CapsuleEndB.Get()})
	{
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetStaticMesh(Mesh == ThrustHead ? Cone.Object : Mesh == Link ? Cylinder.Object : Sphere.Object);
		Mesh->SetCastShadow(false);
		Mesh->SetVisibility(false);
	}
	Caption = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Caption"));
	Caption->SetupAttachment(RootComponent);
	Caption->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Caption->SetCanEverAffectNavigation(false);
	Caption->SetCastShadow(false);
	Caption->SetHorizontalAlignment(EHTA_Center);
	Caption->SetWorldSize(64);
	Caption->SetVisibility(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	Light->SetupAttachment(RootComponent);
	Light->SetCastShadows(false);
	Light->SetCanEverAffectNavigation(false);
	Light->SetVisibility(false);
}

void ALSShellDetonationCue::Initialize(const FLSShellCueSpec& Spec)
{
	SetActorLocation(Spec.Location);
	bSurfaceCharge = Spec.Style == ELSShellCueStyle::Charge && Spec.TargetBody.IsValid();
	if (bSurfaceCharge)
	{
		if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Spec.TargetBody.Get()))
		{
			bCapsuleShell = true;
			FitCapsuleShell(Flash, CapsuleEndA, CapsuleEndB, Capsule);
		}
		else FitBodyShell(Flash, Spec.TargetBody.Get());
	}
	Age = 0;
	Delay = FMath::Max(0.f, Spec.DelaySeconds);
	Radius = FMath::Max(1.f, Spec.Radius);
	Strength = FMath::Max(0.1f, Spec.Strength);
	Style = Spec.Style;
	Direction = Spec.Direction.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	DurationSeconds = FMath::Max(SMALL_NUMBER, Spec.DurationSeconds);
	FlashSeconds = FMath::Max(bSurfaceCharge ? .25f : SMALL_NUMBER, Spec.FlashSeconds);
	if (bSurfaceCharge) DurationSeconds = FMath::Max(DurationSeconds, FlashSeconds);
	PitchMultiplier = Spec.PitchMultiplier;
	CaptionHeight = Spec.CaptionHeight;
	Sound = Spec.Sound;
	if (Style == ELSShellCueStyle::Legacy)
	{
		Ignition->SetWorldScale3D(FVector(.65f * FMath::Sqrt(Strength)));
		SetCueMaterial(Ignition, this, FLinearColor(3, 1.6f, .4f), 1);
	}
	if (Style == ELSShellCueStyle::Thrust)
	{
		Flash->SetWorldRotation(FRotationMatrix::MakeFromX(Direction).Rotator());
		Flash->SetWorldScale3D(FVector(.35f * Radius / 50, .2f * FMath::Sqrt(Strength), .2f * FMath::Sqrt(Strength)));
		Flash->SetWorldLocation(GetActorLocation() - Direction * (50 + Radius * .35f));
		ThrustHead->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
		ThrustHead->SetWorldLocation(GetActorLocation() - Direction * 25);
		ThrustHead->SetWorldScale3D(FVector(.45f, .45f, .5f));
	}
	else if (Style == ELSShellCueStyle::Contact || Style == ELSShellCueStyle::Shatter)
	{
		Flash->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
		Flash->SetWorldScale3D(FVector(.3f * Radius / 50, .3f * Radius / 50, .15f * FMath::Sqrt(Strength)));
		Shockwave->SetWorldRotation(FRotator::ZeroRotator);
	}
	else if (Style == ELSShellCueStyle::Charge)
	{
		if (!bSurfaceCharge) Flash->SetWorldScale3D(FVector(.25f * Radius / 50));
	}
	else if (Style == ELSShellCueStyle::Discharge)
	{
		Flash->SetWorldScale3D(FVector(.15f, .15f, .6f) * FMath::Sqrt(Strength));
	}
	else if (Style != ELSShellCueStyle::Legacy)
	{
		Flash->SetWorldScale3D(FVector(.5f));
	}
	if (Style != ELSShellCueStyle::Legacy)
		Shockwave->SetWorldScale3D(FVector(.06f * Radius / 50, .06f * Radius / 50, .12f));
	if (Style == ELSShellCueStyle::Charge)
		Shockwave->SetWorldScale3D(FVector(Radius / 50, Radius / 50, .08f));
	Ignition->SetVisibility(Style == ELSShellCueStyle::Legacy);
	FlashMaterial = SetCueMaterial(Flash, this, Spec.Color * 3, 0.85f);
	if (bCapsuleShell && FlashMaterial)
	{
		CapsuleEndA->SetMaterial(0, FlashMaterial);
		CapsuleEndB->SetMaterial(0, FlashMaterial);
	}
	RingMaterial = SetCueMaterial(Shockwave, this, Spec.Color * 1.8f, 0.28f, true);
	LinkMaterial = SetCueMaterial(Link, this, Spec.Color * 2, 0.8f);
	if (Style == ELSShellCueStyle::Thrust) HeadMaterial = SetCueMaterial(ThrustHead, this, Spec.Color * 3, .85f);
	if (Spec.bDrawLink)
	{
		const FVector Delta = Spec.Location - Spec.Origin;
		Link->SetWorldLocation((Spec.Location + Spec.Origin) * 0.5f);
		Link->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta).Rotator());
		Link->SetWorldScale3D(FVector(0.065f, 0.065f, Delta.Size() / 100));
		Link->SetVisibility(!Delta.IsNearlyZero());
	}
	Caption->SetText(FText::FromString(Spec.Caption));
	Caption->SetTextRenderColor(Spec.Color.ToFColor(true));
	Caption->SetRelativeLocation(FVector(0, 0, CaptionHeight));
	if (Style != ELSShellCueStyle::Legacy)
		if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
			Caption->SetWorldRotation((Camera->GetCameraLocation() - Caption->GetComponentLocation()).Rotation());
	Light->SetLightColor(Spec.Color);
	Light->SetAttenuationRadius(Spec.LightRadius > 0 ? Spec.LightRadius : FMath::Min(Radius, 900.f));
	SetLifeSpan(Delay + DurationSeconds);
	if (Delay == 0) ActivateBurst();
}

void ALSShellDetonationCue::InitializeStoredCharge(ULSShellReactiveComponent* Reactive, UPrimitiveComponent* Body)
{
	if (!IsValid(Reactive) || !IsValid(Body)) { Destroy(); return; }
	bStoredCharge = true;
	ChargeReactive = Reactive;
	ChargeBody = Body;
	ChargeGeneration = Reactive->GetBodyGeneration();
	if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Body))
	{
		bCapsuleShell = true;
		FitCapsuleShell(Shockwave, CapsuleEndA, CapsuleEndB, Capsule);
	}
	else FitBodyShell(Shockwave, Body);
	FitBodyShell(Flash, Body);
	if (Cast<UCapsuleComponent>(Body) || Cast<USphereComponent>(Body))
		Flash->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	FVector BandScale = Flash->GetRelativeScale3D();
	BandScale.Z = .24f;
	Flash->SetRelativeScale3D(BandScale);
	FlashMaterial = SetCueMaterial(Flash, this, FLinearColor(.2f, 2.5f, 4), .3f, true);
	Flash->SetVisibility(true);
	RingMaterial = SetCueMaterial(Shockwave, this, FLinearColor(.2f, 2.5f, 4), .35f, true);
	if (bCapsuleShell && RingMaterial)
	{
		CapsuleEndA->SetMaterial(0, RingMaterial);
		CapsuleEndB->SetMaterial(0, RingMaterial);
		CapsuleEndA->SetVisibility(true);
		CapsuleEndB->SetVisibility(true);
	}
	Shockwave->SetVisibility(true);
}

void ALSShellDetonationCue::ActivateBurst()
{
	bActive = true;
	Ignition->SetVisibility(false);
	Flash->SetVisibility(true);
	CapsuleEndA->SetVisibility(bCapsuleShell);
	CapsuleEndB->SetVisibility(bCapsuleShell);
	ThrustHead->SetVisibility(Style == ELSShellCueStyle::Thrust);
	Shockwave->SetVisibility(Style != ELSShellCueStyle::Thrust && Style != ELSShellCueStyle::Discharge && !bSurfaceCharge);
	Caption->SetVisibility(true);
	Light->SetVisibility(true);
	if (Sound) UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), FMath::Min(Strength, 1.5f), PitchMultiplier);
}

void ALSShellDetonationCue::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (bStoredCharge)
	{
		const ULSShellReactiveComponent* Reactive = ChargeReactive.Get();
		const ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
		if (!Reactive || !ChargeBody.IsValid() || Reactive->GetPhysicalBody() != ChargeBody.Get() ||
			Reactive->GetBodyGeneration() != ChargeGeneration || !Reactive->IsPhysicalBodyPresent() ||
			Reactive->GetStoredCharge() <= 0 || !Coordinator || !Coordinator->IsBattleActive())
		{
			Destroy();
			return;
		}
		if (RingMaterial) RingMaterial->SetScalarParameterValue(TEXT("Opacity"), .16f + .08f * (1 + FMath::Sin(Age * 4)));
		if (FlashMaterial) FlashMaterial->SetScalarParameterValue(TEXT("Opacity"), .35f + .12f * (1 + FMath::Sin(Age * 4)));
		return;
	}
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
		Caption->SetWorldRotation((Camera->GetCameraLocation() - Caption->GetComponentLocation()).Rotation());
	if (!bActive && Age >= Delay) ActivateBurst();
	if (!bActive) return;
	const float BurstAge = Age - Delay;
	const float T = FMath::Clamp(BurstAge / DurationSeconds, 0.f, 1.f);
	const float FlashT = FMath::Clamp(BurstAge / FlashSeconds, 0.f, 1.f);
	if (Style == ELSShellCueStyle::Thrust)
	{
		Flash->SetWorldScale3D(FVector(FMath::Lerp(0.35f, 1.f, FlashT) * Radius / 50,
			0.2f * FMath::Sqrt(Strength), 0.2f * FMath::Sqrt(Strength)));
		// 불투명한 몸체 내부를 피하고 실제 입사 경로에만 화살표를 놓는다. 끝은 피격 표면이다.
		Flash->SetWorldLocation(GetActorLocation() - Direction * (50 + FMath::Lerp(.35f, 1.f, FlashT) * Radius));
		ThrustHead->SetVisibility(FlashT < 1);
	}
	else if (Style == ELSShellCueStyle::Contact || Style == ELSShellCueStyle::Shatter)
	{
		const float Width = FMath::Lerp(0.3f, 1.f, FlashT) * Radius / 50;
		Flash->SetWorldScale3D(FVector(Width, Width, 0.15f * FMath::Sqrt(Strength)));
	}
	else if (Style == ELSShellCueStyle::Charge)
	{
		if (!bSurfaceCharge) Flash->SetWorldScale3D(FVector(FMath::Lerp(.25f, .1f, FlashT) * Radius / 50));
	}
	else if (Style == ELSShellCueStyle::Discharge)
	{
		Flash->SetWorldScale3D(FVector(.15f, .15f, FMath::Lerp(.6f, 1.2f, FlashT)) * FMath::Sqrt(Strength));
	}
	else
	{
		Flash->SetWorldScale3D(FVector(FMath::Lerp(0.5f, 2.5f * FMath::Sqrt(Strength), FlashT)));
	}
	Flash->SetVisibility(FlashT < 1);
	CapsuleEndA->SetVisibility(bCapsuleShell && FlashT < 1);
	CapsuleEndB->SetVisibility(bCapsuleShell && FlashT < 1);
	Shockwave->SetWorldScale3D(FVector(FMath::Lerp(0.06f, 1.f, FMath::Sqrt(T)) * Radius / 50,
		FMath::Lerp(0.06f, 1.f, FMath::Sqrt(T)) * Radius / 50, 0.12f));
	if (Style == ELSShellCueStyle::Charge)
		Shockwave->SetWorldScale3D(FVector(FMath::Lerp(1.f, .1f, T) * Radius / 50,
			FMath::Lerp(1.f, .1f, T) * Radius / 50, .08f));
	Caption->SetRelativeLocation(FVector(0, 0, CaptionHeight + (Style == ELSShellCueStyle::Legacy ? 85 * T : 0)));
	Caption->SetWorldScale3D(FVector(Style == ELSShellCueStyle::Legacy ? FMath::Lerp(1.f, 1.15f, T) : 1));
	Light->SetIntensity((1 - FlashT) * 18000 * Strength);
	if (FlashMaterial) FlashMaterial->SetScalarParameterValue(TEXT("Opacity"), (1 - FlashT) * 0.85f);
	if (HeadMaterial) HeadMaterial->SetScalarParameterValue(TEXT("Opacity"), (1 - FlashT) * .85f);
	if (RingMaterial) RingMaterial->SetScalarParameterValue(TEXT("Opacity"), (1 - T) * 0.32f);
	if (LinkMaterial) LinkMaterial->SetScalarParameterValue(TEXT("Opacity"), (1 - T) * 0.8f);
	if (T >= 1) Destroy();
}
