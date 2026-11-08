#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellWeakPoint.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "ShellExperiment/LSShellObstacleResponseComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Castle/LSCastle.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ALSShellExperimentEnemy::ALSShellExperimentEnemy()
{
	SetInitialFaction(ELSFaction::Enemy);
	Orders->PrimaryComponentTick.bStartWithTickEnabled = false;
	Orders->SetComponentTickEnabled(false);
	Behavior = CreateDefaultSubobject<ULSEnemyBehaviorComponent>(TEXT("EnemyBehavior"));
	Behavior->Role = ELSEnemyRole::Assault;
	Behavior->bStartRegionallyDormant = false;
	Reactive = CreateDefaultSubobject<ULSShellReactiveComponent>(TEXT("Reactive"));
	MaterialState = CreateDefaultSubobject<ULSShellMaterialStateComponent>(TEXT("MaterialState"));
	Siege = CreateDefaultSubobject<ULSShellSiegeController>(TEXT("Siege"));
	ObstacleResponse = CreateDefaultSubobject<ULSShellObstacleResponseComponent>(TEXT("ObstacleResponse"));
	Health->bDestroyOnDeath = true;
	Health->DestroyDelay = 0;
	bUseRagdoll = false;
	GetCapsuleComponent()->InitCapsuleSize(42, 88);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCanEverAffectNavigation(false);
	Vision->bConcealedOutsideSight = false;
	Vision->bProvidesSight = false;
	Vision->bTriggersEnemyDiscovery = false;
	BodyVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyVisual"));
	BodyVisual->SetupAttachment(GetCapsuleComponent());
	BodyVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyVisual->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Shape.Succeeded()) BodyVisual->SetStaticMesh(Shape.Object);
	BodyVisual->SetRelativeScale3D(FVector(0.7, 0.7, 1.5));
	HeavyProjectile.ProjectileClass = ALSBasicProjectile::StaticClass();
	HeavyProjectile.VisualStyle = ELSProjectileVisualStyle::BallistaBolt;
	WeakPointClass = ALSShellWeakPoint::StaticClass();
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Wheel%d"), Index));
		Wheel->SetupAttachment(GetCapsuleComponent());
		Wheel->SetStaticMesh(Cylinder.Object);
		Wheel->SetRelativeRotation(FRotator(0, 0, 90));
		Wheel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wheel->SetCanEverAffectNavigation(false);
		Wheel->SetVisibility(false);
		Wheels.Add(Wheel);
	}
	CannonAim = CreateDefaultSubobject<USceneComponent>(TEXT("CannonAim"));
	CannonAim->SetupAttachment(GetCapsuleComponent());
	CannonVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CannonVisual"));
	CannonVisual->SetupAttachment(CannonAim);
	CannonVisual->SetStaticMesh(Cylinder.Object);
	CannonVisual->SetRelativeLocation(FVector(110, 0, 0));
	CannonVisual->SetRelativeRotation(FRotator(90, 0, 0));
	CannonVisual->SetRelativeScale3D(FVector(.22, .22, 2.2));
	CannonVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CannonVisual->SetCanEverAffectNavigation(false);
	CannonVisual->SetVisibility(false);
	RoleLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RoleLabel"));
	RoleLabel->SetupAttachment(GetCapsuleComponent());
	RoleLabel->SetAbsolute(false, true, false);
	RoleLabel->SetRelativeRotation(FRotator(55, 90, 0));
	RoleLabel->SetHorizontalAlignment(EHTA_Center);
	RoleLabel->SetVerticalAlignment(EVRTA_TextCenter);
	RoleLabel->SetWorldSize(23);
	RoleLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoleLabel->SetCanEverAffectNavigation(false);
	RoleLabel->SetCastShadow(false);
	RoleLabel->SetVisibility(false);
}

void ALSShellExperimentEnemy::ResolveStatsRow()
{
	StatsRow.DataTable = StatsTable;
	StatsRow.RowName = StatsRowName.IsNone()
		? FName(EnemyKind == ELSShellEnemyKind::Carrier ? TEXT("Carrier") :
			EnemyKind == ELSShellEnemyKind::Heavy ? TEXT("Heavy") : TEXT("Assault")) : StatsRowName;
}

void ALSShellExperimentEnemy::OnConstruction(const FTransform& Transform)
{
	ResolveStatsRow();
	PrepareHeavyDefinition();
	RefreshRoleVisuals();
	Super::OnConstruction(Transform);
}

void ALSShellExperimentEnemy::PostLoad()
{
	Super::PostLoad();
	ResolveStatsRow();
	PrepareHeavyDefinition();
	RefreshRoleVisuals();
}

#if WITH_EDITOR
void ALSShellExperimentEnemy::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	ResolveStatsRow();
	PrepareHeavyDefinition();
	RefreshRoleVisuals();
	Super::PostEditChangeProperty(Event);
}
#endif

void ALSShellExperimentEnemy::BeginPlay()
{
	ResolveStatsRow();
	PrepareHeavyDefinition();
	RefreshRoleVisuals();
	const bool bProjectileReady = !IsHeavy() || (HeavyProjectile.ProjectileClass && BasicAttack->ConfigureProjectile(HeavyProjectile));
	Behavior->Role = ELSEnemyRole::Assault;
	Behavior->bStartRegionallyDormant = false;
	if (EnemyKind == ELSShellEnemyKind::Carrier)
	{
		Reactive->bExplosive = true;
		Reactive->DetonationRadius = CarrierExplosionRadius;
		BodyVisual->SetRelativeScale3D(FVector(1, 0.75, 0.65));
	}
	Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnDied);
	BasicAttack->OnAttackExecutedEvent.AddDynamic(this, &ThisClass::OnAttackExecuted);
	Super::BeginPlay();
	Orders->SetComponentTickEnabled(false);
	if (EnemyKind == ELSShellEnemyKind::Carrier && BasicAttack->IsConfigured())
	{
		FLSBasicAttackSettings Settings = BasicAttack->GetAttackSettings();
		Settings.Damage = 0;
		BasicAttack->ConfigureAttack(Settings);
	}
	const ALSShellExperimentGameMode* Mode = Cast<ALSShellExperimentGameMode>(GetWorld()->GetAuthGameMode());
	if (!Mode || !Mode->IsConfigurationValid() || !bProjectileReady)
	{
		EndBattleActions();
		return;
	}
	if (IsHeavy() && ULSCombatTargetLibrary::IsCombatAlive(this) && !SpawnWeakPoint())
	{
		UE_LOG(LogTemp, Error, TEXT("Shell experiment heavy failed to prepare weak point: %s"), *GetPathName());
		EndBattleActions();
	}
	if (auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()); Coordinator && Coordinator->IsSupplyExperiment())
	{
		ObstacleResponse->SetStrategicTarget(Coordinator->PlayerCastle);
		if (IsHeavy() && !Siege->BeginSiege(Coordinator->PlayerCastle, Siege->DeploymentLocation)) EndBattleActions();
	}
}

void ALSShellExperimentEnemy::PrepareHeavyDefinition()
{
	if (!IsHeavy()) return;
	GetCapsuleComponent()->SetCapsuleSize(140, 140, false);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(true);
	GetCharacterMovement()->UpdateNavAgent(*GetCapsuleComponent());
	Reactive->Mass = HeavyMass;
	Reactive->MovementResistance = HeavyMovementResistance;
	Reactive->bExplosive = false;
	Reactive->bConductiveRelay = true;
	BodyVisual->SetRelativeScale3D(FVector(2.4, 2.4, 2.2));
}

void ALSShellExperimentEnemy::RefreshRoleVisuals()
{
	const bool bCarrier = EnemyKind == ELSShellEnemyKind::Carrier;
	const bool bVehicle = bCarrier || IsHeavy();
	for (int32 Index = 0; Index < Wheels.Num(); ++Index)
	{
		Wheels[Index]->SetVisibility(bVehicle);
		Wheels[Index]->SetRelativeLocation(FVector((Index < 2 ? -1 : 1) * (bCarrier ? 32 : 80),
			(Index % 2 ? -1 : 1) * (bCarrier ? 42 : 115), bCarrier ? -65 : -95));
		Wheels[Index]->SetRelativeScale3D(bCarrier ? FVector(.46, .46, .13) : FVector(.9, .9, .3));
	}
	CannonVisual->SetVisibility(IsHeavy());
	RoleLabel->SetVisibility(bVehicle);
	RoleLabel->SetRelativeLocation(bCarrier ? FVector(0, 55, 35) : FVector(0, 120, 125));
	RoleLabel->SetText(FText::FromString(bCarrier ? TEXT("CARRIER") : TEXT("HEAVY")));
	RoleLabel->SetTextRenderColor(bCarrier ? FColor(255, 210, 110) : FColor(150, 230, 255));
	if (!bVehicle) return;
	BodyVisual->SetRelativeLocation(bCarrier ? FVector(0, 0, -30) : FVector::ZeroVector);
	if (bCarrier) BodyVisual->SetRelativeScale3D(FVector(1, .75, .65));
	if (!RoleMaterial || !TrimMaterial)
	{
		if (UMaterialInterface* Source = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Landship/ShellExperiment/Materials/MI_ShellFixed.MI_ShellFixed")))
		{
			RoleMaterial = UMaterialInstanceDynamic::Create(Source, this);
			TrimMaterial = UMaterialInstanceDynamic::Create(Source, this);
		}
	}
	if (RoleMaterial)
	{
		RoleMaterial->SetVectorParameterValue(TEXT("TeamColor"), bCarrier ? FLinearColor(1, .27f, .015f) : FLinearColor(.055f, .075f, .10f));
		BodyVisual->SetMaterial(0, RoleMaterial);
	}
	if (TrimMaterial)
	{
		TrimMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.12f, .14f, .18f));
		for (UStaticMeshComponent* Wheel : Wheels) Wheel->SetMaterial(0, TrimMaterial);
		CannonVisual->SetMaterial(0, TrimMaterial);
	}
}

bool ALSShellExperimentEnemy::SpawnWeakPoint()
{
	if (!WeakPointClass) return false;
	const FVector Location = GetActorTransform().TransformPosition(FVector(0, 0, 220));
	const FTransform Transform(GetActorQuat(), Location);
	WeakPoint = GetWorld()->SpawnActorDeferred<ALSShellWeakPoint>(WeakPointClass, Transform, this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(WeakPoint)) return false;
	if (!WeakPoint->SetInitialHeavy(this))
	{
		RemoveWeakPoint();
		return false;
	}
	// 이동 무시 목록은 명중 질의의 무시 목록이 아니다. 외부 포탄은 약점을 계속 맞힌다.
	GetCapsuleComponent()->IgnoreActorWhenMoving(WeakPoint, true);
	WeakPoint->Body->IgnoreActorWhenMoving(this, true);
	WeakPoint->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepWorldTransform);
	WeakPoint->FinishSpawning(Transform);
	if (!IsValid(WeakPoint) || !ULSCombatTargetLibrary::IsCombatAlive(WeakPoint))
	{
		RemoveWeakPoint();
		return false;
	}
	return true;
}

void ALSShellExperimentEnemy::RemoveWeakPoint()
{
	if (IsValid(WeakPoint)) WeakPoint->RemoveWithoutDetonation();
	WeakPoint = nullptr;
}

void ALSShellExperimentEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
	RemoveWeakPoint();
	Super::EndPlay(Reason);
}

void ALSShellExperimentEnemy::NotifyHeavyDefeatedOnce()
{
	if (bHeavyDefeatedNotified) return;
	bHeavyDefeatedNotified = true;
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->NotifyHeavyDefeated();
}

void ALSShellExperimentEnemy::CommitWeakPointDetonation(ALSShellWeakPoint* Sender, const FLSDamageResult& Damage)
{
	ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!IsHeavy() || Sender != WeakPoint || bWeakPointDetonationCommitted || !Coordinator ||
		!Coordinator->IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(this)) return;
	bWeakPointDetonationCommitted = true;
	FLSShellExplosion Explosion;
	Coordinator->CaptureShotContext(Explosion.Shot, this);
	Explosion.Location = Sender->GetActorLocation();
	Explosion.Damage = WeakPointExplosionDamage;
	Explosion.Radius = WeakPointExplosionRadius;
	Explosion.Velocity = WeakPointExplosionVelocity;
	Explosion.Trigger = ELSShellDetonationTrigger::WeakPoint;
	Explosion.TriggerOrigin = Damage.AttackOrigin;
	Explosion.bHasTriggerOrigin = Damage.bHasAttackOrigin;
	Explosion.Shot.Source = this;
	Explosion.Shot.Faction = GetFaction();
	Explosion.Shot.Origin = Explosion.Location;
	Coordinator->QueueExplosion(Explosion);
	NotifyHeavyDefeatedOnce();
}

void ALSShellExperimentEnemy::OnAttackExecuted(const FLSAttackExecution& Execution)
{
	if (IsHeavy() && Execution.Source.Get() == this && !Execution.Direction.IsNearlyZero())
		CannonAim->SetWorldRotation(Execution.Direction.Rotation());
	if (EnemyKind == ELSShellEnemyKind::Carrier && Execution.Source.Get() == this)
		Reactive->RequestDetonationWithCause(ELSShellDetonationTrigger::Proximity, Execution.TargetLocation);
}

void ALSShellExperimentEnemy::OnDied(const FLSDamageResult& Damage)
{
	if (auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()); Coordinator && Coordinator->IsSupplyExperiment())
		Coordinator->HandleMaterialDeath(this);
	if (IsHeavy())
	{
		RemoveWeakPoint();
		NotifyHeavyDefeatedOnce();
	}
	if (ALSShellExperimentCoordinator* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld()))
		Coordinator->ShowPulse(GetActorLocation(), 110, FLinearColor(1, 0.3f, 0.05f));
}
