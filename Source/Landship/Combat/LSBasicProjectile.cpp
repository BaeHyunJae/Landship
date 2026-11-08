#include "Combat/LSBasicProjectile.h"
#include "Combat/LSPhysicalBlocker.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSFactionInterface.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ALSBasicProjectile::ALSBasicProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetSphereRadius(6.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(LSCollisionChannels::Projectile);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	Shaft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shaft"));
	Shaft->SetupAttachment(Collision);
	Shaft->SetCanEverAffectNavigation(false);
	Shaft->SetStaticMesh(Cube.Object);
	Shaft->SetRelativeLocation(FVector(-30, 0, 0));
	Shaft->SetRelativeScale3D(FVector(0.6f, 0.04f, 0.04f));
	Shaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shaft->SetCanEverAffectNavigation(false);
	Tip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tip"));
	Tip->SetupAttachment(Collision);
	Tip->SetCanEverAffectNavigation(false);
	Tip->SetStaticMesh(Cone.Object);
	Tip->SetRelativeLocation(FVector(10, 0, 0));
	Tip->SetRelativeRotation(FRotator(90, 0, 0));
	Tip->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.2f));
	Tip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tip->SetCanEverAffectNavigation(false);
}

void ALSBasicProjectile::SetVisualStyle(ELSProjectileVisualStyle Style)
{
	if (bLaunched)
		return;
	VisualStyle = Style;
	if (Style == ELSProjectileVisualStyle::TurretBolt)
	{
		Shaft->SetRelativeLocation(FVector(-12, 0, 0));
		Shaft->SetRelativeScale3D(FVector(0.32f, 0.14f, 0.14f));
		Tip->SetRelativeLocation(FVector(12, 0, 0));
		Tip->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.16f));
	}
	else if (Style == ELSProjectileVisualStyle::BallistaBolt)
	{
		Shaft->SetRelativeLocation(FVector(-45, 0, 0));
		Shaft->SetRelativeScale3D(FVector(0.9f, 0.09f, 0.09f));
		Tip->SetRelativeLocation(FVector(15, 0, 0));
		Tip->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.3f));
	}
	else
	{
		Shaft->SetRelativeLocation(FVector(-30, 0, 0));
		Shaft->SetRelativeScale3D(FVector(0.6f, 0.04f, 0.04f));
		Tip->SetRelativeLocation(FVector(10, 0, 0));
		Tip->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.2f));
	}
}

bool ALSBasicProjectile::Launch(const FLSDamageRequest& Request, AActor* Target, const FVector& Direction, float Speed,
                                float Radius, float Lifetime, UMaterialInterface* Material)
{
	if (!IsValid(Target) || !ULSCombatTargetLibrary::IsCombatAlive(Target) ||
	    !InitializeFlight(Request, Direction, Speed, Radius, Lifetime, Material))
		return false;
	bStopsAtWorldGeometry = true;
	return true;
}

bool ALSBasicProjectile::LaunchInDirection(const FLSDamageRequest& Request, const FVector& Direction, float Speed,
                                          float Radius, float Lifetime, UMaterialInterface* Material)
{
	if (!Request.bHasSourceFaction || !LSFaction::IsValid(Request.SourceFaction) ||
	    !InitializeFlight(Request, Direction, Speed, Radius, Lifetime, Material))
		return false;
	bStopsAtWorldGeometry = true;
	return true;
}

bool ALSBasicProjectile::LaunchFirstBlocking(const FLSDamageRequest& Request, const FVector& Direction, float Speed,
                                             float Radius, float Lifetime, UMaterialInterface* Material)
{
	if (!LaunchInDirection(Request, Direction, Speed, Radius, Lifetime, Material))
		return false;
	bHitsFirstBlocking = true;
	return true;
}

bool ALSBasicProjectile::InitializeFlight(const FLSDamageRequest& Request, const FVector& Direction, float Speed,
                                          float Radius, float Lifetime, UMaterialInterface* Material)
{
	if (bLaunched || Direction.ContainsNaN() || Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0 ||
	    !FMath::IsFinite(Radius) || Radius <= 0 || !FMath::IsFinite(Lifetime) || Lifetime <= 0 ||
	    !FMath::IsFinite(Request.DamageAmount) || Request.DamageAmount < 0 ||
	    (Request.bHasSourceFaction && !LSFaction::IsValid(Request.SourceFaction)))
		return false;
	ShotRequest = Request;
	OriginalSource = Request.SourceActor;
	// 공격자를 강하게 보유하지 않는다. 제거된 뒤에도 저장된 진영으로 오폭을 판정한다.
	ShotRequest.SourceActor = nullptr;
	ShotRequest.DamageCauser = nullptr;
	FlightOrigin = GetActorLocation();
	ShotRequest.bHasAttackOrigin = true;
	ShotRequest.AttackOrigin = FlightOrigin;
	FlightVelocity = Direction.GetSafeNormal() * Speed;
	Collision->SetSphereRadius(Radius);
	SetActorRotation(FlightVelocity.Rotation());
	RemainingFlightTime = Lifetime;
	bLaunched = true;
	SetLifeSpan(Lifetime);
	if (!Material)
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (Material)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		const FLinearColor Color = VisualStyle == ELSProjectileVisualStyle::TurretBolt
			? FLinearColor(1.0f, 0.28f, 0.015f)
			: (Request.SourceFaction == ELSFaction::Ally ? FLinearColor(0.08f, 0.55f, 1.0f)
			                                          : FLinearColor(0.95f, 0.12f, 0.05f));
		Instance->SetVectorParameterValue(TEXT("TeamColor"), Color);
		Shaft->SetMaterial(0, Instance);
		Tip->SetMaterial(0, Instance);
	}
	return true;
}

void ALSBasicProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (!bLaunched)
	{
		Destroy();
	}
}

void ALSBasicProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLaunched || bResolved || !GetWorld() || GetWorld()->bIsTearingDown || !FMath::IsFinite(DeltaSeconds) ||
	    DeltaSeconds <= 0)
	{
		return;
	}
	const float TravelSeconds = FMath::Min(DeltaSeconds, RemainingFlightTime);
	const FVector Start = GetActorLocation();
	const FVector End = Start + FlightVelocity * TravelSeconds;
	if (bHitsFirstBlocking)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSObstacleBallista), false, this);
		Query.bFindInitialOverlaps = true;
		if (OriginalSource.IsValid())
			Query.AddIgnoredActor(OriginalSource.Get());
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
		                                      LSCollisionChannels::ObstacleProjectile,
		                                      FCollisionShape::MakeSphere(Collision->GetScaledSphereRadius()), Query) &&
		    (Hit.bBlockingHit || Hit.bStartPenetrating) && IsValid(Hit.GetComponent()))
		{
			ResolveHit(Hit);
			return;
		}
	}
	else
	{
		FHitResult Hit;
		if (FindFirstHit(Start, End, Hit))
		{
			ResolveHit(Hit);
			return;
		}
	}
	SetActorLocation(End, false);
	RemainingFlightTime -= TravelSeconds;
	if (RemainingFlightTime <= 0)
	{
		Destroy();
	}
}

FLSDamageRequest ALSBasicProjectile::MakeHitRequest() const
{
	FLSDamageRequest Request = ShotRequest;
	AActor* Source = OriginalSource.Get();
	Request.SourceActor = IsValid(Source) && !Source->IsActorBeingDestroyed() ? Source : nullptr;
	return Request;
}

bool ALSBasicProjectile::FindFirstHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBasicProjectile), false, this);
	if (OriginalSource.IsValid())
		Query.AddIgnoredActor(OriginalSource.Get());
	const FLSDamageRequest Request = MakeHitRequest();
	bool bSkipped;
	bool bHasDamageHit = false;
	do
	{
		bSkipped = false;
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
		                                   FCollisionShape::MakeSphere(Collision->GetScaledSphereRadius()), Query);
		Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
		for (const FHitResult& Hit : Hits)
		{
			if (bStopsAtWorldGeometry && IsValid(Hit.GetComponent()) &&
				Hit.GetComponent()->GetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile) == ECR_Ignore)
			{
				Query.AddIgnoredComponent(Hit.GetComponent());
				bSkipped = true;
				continue;
			}
			AActor* Target = Hit.GetActor();
			if (!IsValid(Target))
				continue;
			if (LSPhysicalBlocker::IsBlockingBody(Hit.GetComponent()))
			{
				OutHit = Hit;
				bHasDamageHit = true;
				break;
			}
			const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Target);
			if (!Target->IsActorBeingDestroyed() && ULSCombatTargetLibrary::IsCombatAlive(Target) && Faction &&
			    LSFaction::IsValid(Faction->GetFaction()) &&
			    ULSHealthComponent::EvaluateDamagePolicy(Request, Target) == ELSApplyDamageStatus::Applied)
			{
				OutHit = Hit;
				bHasDamageHit = true;
				break;
			}
			Query.AddIgnoredActor(Target);
			bSkipped = true;
		}
	} while (bSkipped && !bHasDamageHit);
	if (!bStopsAtWorldGeometry)
		return bHasDamageHit;

	FCollisionQueryParams GeometryQuery(SCENE_QUERY_STAT(LSBasicProjectileCover), false, this);
	GeometryQuery.bFindInitialOverlaps = true;
	if (OriginalSource.IsValid())
		GeometryQuery.AddIgnoredActor(OriginalSource.Get());
	FHitResult GeometryHit;
	while (GetWorld()->SweepSingleByChannel(GeometryHit, Start, End, FQuat::Identity,
		LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(Collision->GetScaledSphereRadius()),
		GeometryQuery) && (GeometryHit.bBlockingHit || GeometryHit.bStartPenetrating))
	{
		AActor* Actor = GeometryHit.GetActor();
		// 명시적 물리 차단만 체력·진영 통과 정책보다 우선한다. 그 밖의 전투 몸체는 기존 정책을 유지한다.
		if (!LSPhysicalBlocker::IsBlockingBody(GeometryHit.GetComponent()) && IsValid(Actor) &&
			Cast<ILSFactionInterface>(Actor) && Actor->FindComponentByClass<ULSHealthComponent>())
		{
			GeometryQuery.AddIgnoredActor(Actor);
			continue;
		}
		if (!bHasDamageHit || GeometryHit.Time <= OutHit.Time)
			OutHit = GeometryHit;
		return true;
	}
	return bHasDamageHit;
}

void ALSBasicProjectile::ResolveHit(const FHitResult& Hit)
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;
	SetActorTickEnabled(false);
	SetActorLocation(Hit.Location, false);
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	AActor* Target = Hit.GetActor();
	ULSHealthComponent* Health = IsValid(Target) ? Target->FindComponentByClass<ULSHealthComponent>() : nullptr;
	const ILSFactionInterface* Faction = IsValid(Target) ? Cast<ILSFactionInterface>(Target) : nullptr;
	const bool bCanDamage = Faction && LSFaction::IsValid(Faction->GetFaction()) &&
	    ULSCombatTargetLibrary::IsCombatAlive(Target);
	if (Health && bCanDamage)
	{
		FLSDamageRequest Request = MakeHitRequest();
		Request.DamageCauser = this;
		Request.bHasImpactLocation = true;
		Request.ImpactLocation = Hit.ImpactPoint;
		Request.bHasIncomingDirection = true;
		Request.IncomingDirection = FlightVelocity.GetSafeNormal();
		const FLSApplyDamageResult Result = Health->ApplyDamage(Request);
		if (IsValid(this) && !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown &&
		    Result.AppliedDamage > 0)
		{
			OnDamageApplied.Broadcast(IsValid(Target) ? Target : nullptr, Result.AppliedDamage);
		}
	}
	if (IsValid(this) && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}
