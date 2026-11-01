#include "Obstacles/LSTurret.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Castle/LSCastle.h"
#include "Castle/LSMobileCastle.h"
#include "Troops/LSUnit.h"
#include "Controls/LSSelectableComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
ALSTurret::ALSTurret()
{
	UBoxComponent* Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(70, 70, 120));
	SetObstacleBody(Body, true);
	BasicAttack = CreateDefaultSubobject<ULSBasicAttackComponent>(TEXT("BasicAttack"));
	AttackSettings.Damage = 50;
	AttackSettings.Interval = 2;
	AttackSettings.Range = 1200;
	ProjectileSettings.ProjectileClass = ALSBasicProjectile::StaticClass();
	Vision->bTriggersEnemyDiscovery = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Mesh.Object);
	Visual->SetRelativeScale3D(FVector(1.4, 1.4, 2.4));
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
	if (!bConfigured || !IsObstacleActive())
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
		const bool bCastle = Candidate->IsA<ALSCastle>() || Candidate->IsA<ALSMobileCastle>();
		const ALSUnit* Unit = Cast<ALSUnit>(Candidate);
		if (!bCastle && (!Unit || Unit->Selection->Kind != ELSSelectionKind::Unit))
			continue;
		if (!ULSCombatTargetLibrary::CanAutoAttackTarget(this, Candidate))
			continue;
		const double Distance = ULSCombatTargetLibrary::GetAttackDistance(this, Candidate);
		if (Distance > AttackSettings.Range)
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
	BasicAttack->StopAttack();
	BasicAttack->Deactivate();
}
void ALSTurret::PrepareForDeath()
{
	StopObstacleActions();
	Super::PrepareForDeath();
}
