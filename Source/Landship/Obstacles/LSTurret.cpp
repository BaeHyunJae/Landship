#include "Obstacles/LSTurret.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Castle/LSCastle.h"
#include "Troops/LSUnit.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Controls/LSSelectableComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
ALSTurret::ALSTurret()
{
	PrimaryActorTick.bCanEverTick = true;
	UBoxComponent* Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(70, 70, 120));
	SetObstacleBody(Body, true);
	Body->SetCanEverAffectNavigation(false);
	BasicAttack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("BasicAttack"));
	AttackSettings.Damage = 50;
	AttackSettings.Interval = 2;
	AttackSettings.Range = 1200;
	ProjectileSettings.ProjectileClass = ALSBasicProjectile::StaticClass();
	ProjectileSettings.VisualStyle = ELSProjectileVisualStyle::TurretBolt;
	Vision->bTriggersEnemyDiscovery = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Visual->SetStaticMesh(Cylinder.Object);
	Visual->SetRelativeLocation(FVector(0, 0, -35));
	Visual->SetRelativeScale3D(FVector(1.4, 1.4, 1.7));
	Barrel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Barrel"));
	Barrel->SetupAttachment(Body);
	Barrel->SetStaticMesh(Cylinder.Object);
	Barrel->SetRelativeLocation(FVector(60, 0, 70));
	Barrel->SetRelativeRotation(FRotator(90, 0, 0));
	Barrel->SetRelativeScale3D(FVector(.35, .35, 1.5));
	Barrel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrel->SetCanEverAffectNavigation(false);
}
void ALSTurret::PreRegisterAllComponents()
{
	// 기존 블루프린트·맵에 저장된 설정도 내비게이션에 등록되기 전에 해제한다.
	TInlineComponentArray<UPrimitiveComponent*> Components(this);
	for (UPrimitiveComponent* Component : Components)
		Component->SetCanEverAffectNavigation(false);
	Super::PreRegisterAllComponents();
}
FString ALSTurret::GetConfigurationError() const
{
	const FString Parent = Super::GetConfigurationError();
	if (!Parent.IsEmpty())
		return Parent;
	if (!FMath::IsFinite(AttackSettings.Damage) || AttackSettings.Damage < 0 ||
		!FMath::IsFinite(AttackSettings.Range) || AttackSettings.Range <= 0 ||
		!FMath::IsFinite(AttackSettings.Interval) || AttackSettings.Interval <= 0 ||
		!ProjectileSettings.ProjectileClass)
		return TEXT("Turret requires valid attack settings and a projectile class");
	if (!FMath::IsFinite(ProjectileSettings.Speed) || ProjectileSettings.Speed <= 0 ||
		!FMath::IsFinite(ProjectileSettings.Lifetime) || ProjectileSettings.Lifetime <= 0 ||
		!FMath::IsFinite(ProjectileSettings.Radius) || ProjectileSettings.Radius <= 0 ||
		ProjectileSettings.ProjectileClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		return TEXT("Turret projectile configuration is invalid");
	return FString();
}
void ALSTurret::BeginPlay()
{
	BasicAttack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateUObject(this, &ALSTurret::ResolveTarget));
	const bool bConfigured =
		BasicAttack->ConfigureAttack(AttackSettings) && BasicAttack->ConfigureProjectile(ProjectileSettings);
	Super::BeginPlay();
	if (auto* Material = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype")))
	{
		ThreatMaterial = UMaterialInstanceDynamic::Create(Material, this);
		Visual->SetMaterial(0, ThreatMaterial);
		Barrel->SetMaterial(0, ThreatMaterial);
	}
	UpdateThreatVisual();
	if (!bConfigured || !IsObstacleActive() || LSBattleOutcome::AreActionsStopped(GetWorld()))
		StopObstacleActions();
}
AActor* ALSTurret::ResolveTarget()
{
	if (bActionsStopped || !IsObstacleActive())
		return nullptr;
	AActor* Best = nullptr;
	bool bBestCastle = false;
	float BestHP = 0;
	double BestDistance = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Candidate = *It;
		const ALSUnit* Unit = Cast<ALSUnit>(Candidate);
		const bool bCastle = Candidate->IsA<ALSCastle>() || (Unit && Unit->Selection->Kind == ELSSelectionKind::Castle);
		if (!bCastle && (!Unit || Unit->Selection->Kind != ELSSelectionKind::Unit))
			continue;
		if (!ULSCombatTargetLibrary::CanAutoAttackTarget(this, Candidate))
			continue;
		const double Distance = ULSCombatTargetLibrary::GetAttackDistance(this, Candidate);
		if (Distance > AttackSettings.Range || !BasicAttack->HasClearAttackPathFromLocation(GetActorLocation(), Candidate))
			continue;
		FLSHealthSnapshot HP;
		if (!Candidate->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(HP))
			continue;
		if (!Best || (bCastle && !bBestCastle) ||
			(bCastle == bBestCastle &&
			 ((!bCastle && HP.CurrentHealth < BestHP) ||
			  ((bCastle || HP.CurrentHealth == BestHP) &&
			   (Distance < BestDistance ||
				(Distance == BestDistance && Candidate->GetUniqueID() < Best->GetUniqueID()))))))
		{
			Best = Candidate;
			bBestCastle = bCastle;
			BestHP = HP.CurrentHealth;
			BestDistance = Distance;
		}
	}
	return Best;
}
void ALSTurret::StopObstacleActions()
{
	bActionsStopped = true;
	BasicAttack->EndBattleActions();
	BasicAttack->Deactivate();
	UpdateThreatVisual();
}
void ALSTurret::PrepareForDeath()
{
	StopObstacleActions();
	Super::PrepareForDeath();
}

void ALSTurret::UpdateThreatVisual()
{
	AActor* Target = BasicAttack->GetAttackTarget();
	const bool bThreat = !bActionsStopped && !LSBattleOutcome::AreActionsStopped(GetWorld()) && IsObstacleActive() &&
		ULSCombatTargetLibrary::CanAutoAttackTarget(this, Target) &&
		ULSCombatTargetLibrary::GetAttackDistance(this, Target) <= BasicAttack->GetAttackSettings().Range &&
		BasicAttack->HasClearAttackPathFromLocation(GetActorLocation(), Target);
	if (ThreatMaterial) ThreatMaterial->SetVectorParameterValue(TEXT("TeamColor"),
		bThreat ? FLinearColor(1, .35f, .03f) : FLinearColor(.95f, .12f, .05f));
	if (bThreat)
	{
		const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		Barrel->SetWorldRotation(FRotator(90, Direction.Rotation().Yaw, 0));
		Barrel->SetWorldLocation(GetActorLocation() + Direction * 60 + FVector(0, 0, 70));
	}
}

void ALSTurret::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateThreatVisual();
}

void ALSTurret::EndPlay(const EEndPlayReason::Type Reason)
{
	StopObstacleActions();
	Super::EndPlay(Reason);
}
