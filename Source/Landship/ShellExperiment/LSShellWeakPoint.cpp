#include "ShellExperiment/LSShellWeakPoint.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALSShellWeakPoint::ALSShellWeakPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(55);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(LSCollisionChannels::Building);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCanEverAffectNavigation(false);
	Body->SetGenerateOverlapEvents(false);
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Health->bDestroyOnDeath = true;
	Health->DestroyDelay = 0;
	Vision = CreateDefaultSubobject<ULSVisionParticipantComponent>(TEXT("Vision"));
	Vision->bConcealedOutsideSight = false;
	Vision->bProvidesSight = false;
	Vision->bTriggersEnemyDiscovery = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Body);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Visual->SetStaticMesh(Shape.Object);
	Visual->SetRelativeScale3D(FVector(1.1));
	CoreLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CoreLabel"));
	CoreLabel->SetupAttachment(Body);
	CoreLabel->SetAbsolute(false, true, false);
	CoreLabel->SetRelativeRotation(FRotator(55, 90, 0));
	CoreLabel->SetRelativeLocation(FVector(0, 25, 85));
	CoreLabel->SetHorizontalAlignment(EHTA_Center);
	CoreLabel->SetVerticalAlignment(EVRTA_TextCenter);
	CoreLabel->SetWorldSize(24);
	CoreLabel->SetTextRenderColor(FColor(100, 255, 225));
	CoreLabel->SetText(FText::FromString(TEXT("WEAK")));
	CoreLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CoreLabel->SetCanEverAffectNavigation(false);
	CoreLabel->SetCastShadow(false);
}

bool ALSShellWeakPoint::SetInitialHeavy(ALSShellExperimentEnemy* InHeavy)
{
	if (HasActorBegunPlay() || HeavyParent.IsValid() || !IsValid(InHeavy) || !InHeavy->IsHeavy() ||
		InHeavy->GetWorld() != GetWorld()) return false;
	HeavyParent = InHeavy;
	SetOwner(InHeavy);
	return true;
}

ALSShellExperimentEnemy* ALSShellWeakPoint::GetHeavyParent() const
{
	return HeavyParent.Get();
}

void ALSShellWeakPoint::BeginPlay()
{
	const ALSShellExperimentGameMode* Mode = Cast<ALSShellExperimentGameMode>(GetWorld()->GetAuthGameMode());
	FString ConfigurationError;
	if (!HeavyParent.IsValid() || !HeavyParent->IsHeavy())
		ConfigurationError = TEXT("Weak point requires a Heavy parent configured before FinishSpawning");
	else if (!Mode || !Mode->IsConfigurationValid())
		ConfigurationError = TEXT("Weak point requires a valid shell experiment game mode");
	Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnDied);
	Health->OnDamagedEvent.AddDynamic(this, &ThisClass::OnDamaged);
	if (!Health->InitializeHealth(ELSCombatStatsInitResult::Success, MaxHealth,
		FLSPrepareCombatDeath::CreateUObject(this, &ThisClass::PrepareForDeath), ConfigurationError))
	{
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Vision->SetParticipationEnabled(false);
	}
	FLSHealthSnapshot Snapshot;
	if (Health->TryGetHealth(Snapshot))
		CoreLabel->SetText(FText::FromString(FString::Printf(TEXT("WEAK %d"), FMath::CeilToInt(Snapshot.CurrentHealth))));
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Materials/MI_ShellFixed.MI_ShellFixed")))
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instance->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.05f, 1, .8f));
		Visual->SetMaterial(0, Instance);
	}
	Super::BeginPlay();
}

void ALSShellWeakPoint::PrepareForDeath()
{
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Vision->NotifyCombatDeath();
}

void ALSShellWeakPoint::OnDamaged(const FLSDamageResult& Damage)
{
	CoreLabel->SetText(FText::FromString(FString::Printf(TEXT("WEAK %d"), FMath::CeilToInt(Damage.CurrentHealth))));
}

void ALSShellWeakPoint::OnDied(const FLSDamageResult& Damage)
{
	if (!bRemovedWithoutDetonation && HeavyParent.IsValid()) HeavyParent->CommitWeakPointDetonation(this, Damage);
}

void ALSShellWeakPoint::RemoveWithoutDetonation()
{
	if (bRemovedWithoutDetonation) return;
	bRemovedWithoutDetonation = true;
	PrepareForDeath();
	Destroy();
}
