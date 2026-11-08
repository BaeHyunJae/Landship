#include "ShellExperiment/LSShellExplosive.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALSShellExplosive::ALSShellExplosive()
{
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->InitBoxExtent(FVector(60));
	SetObstacleBody(Body, true);
	Reactive = CreateDefaultSubobject<ULSShellReactiveComponent>(TEXT("Reactive"));
	Reactive->bExplosive = true;
	MaxHealth = 100;
	Vision->bConcealedOutsideSight = false;
	Vision->bProvidesSight = false;
	Vision->bTriggersEnemyDiscovery = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Visual->SetStaticMesh(Cylinder.Object);
	Visual->SetRelativeScale3D(FVector(.82, .82, .94));
	Footing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Footing"));
	Footing->SetupAttachment(Body);
	Footing->SetStaticMesh(Cube.Object);
	Footing->SetRelativeLocation(FVector(0, 0, -53));
	Footing->SetRelativeScale3D(FVector(1.16, 1.16, .14));
	UpperFrame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UpperFrame"));
	LowerFrame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerFrame"));
	for (UStaticMeshComponent* Part : {Footing.Get(), UpperFrame.Get(), LowerFrame.Get()})
	{
		Part->SetupAttachment(Body);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
	}
	UpperFrame->SetStaticMesh(Cylinder.Object);
	UpperFrame->SetRelativeLocation(FVector(0, 0, 44));
	UpperFrame->SetRelativeScale3D(FVector(.88, .88, .08));
	LowerFrame->SetStaticMesh(Cylinder.Object);
	LowerFrame->SetRelativeLocation(FVector(0, 0, -44));
	LowerFrame->SetRelativeScale3D(FVector(.88, .88, .08));
	RoleLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RoleLabel"));
	RoleLabel->SetupAttachment(Body);
	RoleLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoleLabel->SetCanEverAffectNavigation(false);
	RoleLabel->SetCastShadow(false);
	RoleLabel->SetRelativeLocation(FVector(0, 70, 95));
	// TextRender의 앞면은 로컬 +X다. 전장 카메라가 있는 +Y·+Z 방향을 향하게 한다.
	RoleLabel->SetRelativeRotation(FRotator(55, 90, 0));
	RoleLabel->SetWorldSize(23);
	RoleLabel->SetHorizontalAlignment(EHTA_Center);
	RoleLabel->SetVerticalAlignment(EVRTA_TextCenter);
	RoleLabel->SetTextRenderColor(FColor(255, 225, 170));
	RoleLabel->SetText(FText::FromString(TEXT("FIXED")));
}

void ALSShellExplosive::RefreshRoleVisual()
{
	Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bAuxiliaryFacility
		? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Visual->SetRelativeLocation(bAuxiliaryFacility ? FVector(0, 0, -10) : FVector::ZeroVector);
	Visual->SetRelativeScale3D(bAuxiliaryFacility ? FVector(.95, .95, .8) : FVector(.82, .82, .94));
	UpperFrame->SetRelativeLocation(bAuxiliaryFacility ? FVector(-30, 0, 20) : FVector(0, 0, 44));
	LowerFrame->SetRelativeLocation(bAuxiliaryFacility ? FVector(30, 0, 20) : FVector(0, 0, -44));
	const FVector FrameScale = bAuxiliaryFacility ? FVector(.2, .2, .8) : FVector(.88, .88, .08);
	UpperFrame->SetRelativeScale3D(FrameScale);
	LowerFrame->SetRelativeScale3D(FrameScale);
	RoleLabel->SetText(FText::FromString(bAuxiliaryFacility ? TEXT("FACILITY") : TEXT("FIXED")));
	const FLinearColor RoleColor = bAuxiliaryFacility ? FLinearColor(.8f, .08f, .015f) : FLinearColor(1, .27f, .015f);
	if (!RoleMaterial || !BaseMaterial)
	{
		UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Landship/ShellExperiment/Materials/MI_ShellFixed.MI_ShellFixed"));
		if (SourceMaterial)
		{
			RoleMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
			BaseMaterial = UMaterialInstanceDynamic::Create(SourceMaterial, this);
		}
	}
	if (RoleMaterial)
	{
		RoleMaterial->SetVectorParameterValue(TEXT("TeamColor"), RoleColor);
		Visual->SetMaterial(0, RoleMaterial);
	}
	if (BaseMaterial)
	{
		BaseMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.035f, .045f, .06f));
		Footing->SetMaterial(0, BaseMaterial);
		UpperFrame->SetMaterial(0, BaseMaterial);
		LowerFrame->SetMaterial(0, BaseMaterial);
	}
}

void ALSShellExplosive::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshRoleVisual();
}

#if WITH_EDITOR
void ALSShellExplosive::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	RefreshRoleVisual();
	Super::PostEditChangeProperty(Event);
}
#endif

void ALSShellExplosive::BeginPlay()
{
	RefreshRoleVisual();
	Super::BeginPlay();
	const ALSShellExperimentGameMode* Mode = Cast<ALSShellExperimentGameMode>(GetWorld()->GetAuthGameMode());
	if (!Mode || !Mode->IsConfigurationValid()) DisableObstacle();
}

bool ALSShellExplosive::RequestDetonation()
{
	return Reactive->RequestDetonation();
}
