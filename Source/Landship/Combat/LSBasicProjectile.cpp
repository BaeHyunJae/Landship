#include "Combat/LSBasicProjectile.h"
#include "Combat/LSProjectilePoolSubsystem.h"
#include "Combat/LSBasicAttackComponent.h"

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
#include "TimerManager.h"
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

bool ALSBasicProjectile::SupportsPooling() const
{
	return GetClass() == ALSBasicProjectile::StaticClass();
}

bool ALSBasicProjectile::PrepareForPool(ULSProjectilePoolSubsystem* Pool)
{
	if (!IsValid(Pool) || Pool->GetWorld() != GetWorld() || HasActorBegunPlay() || IsPoolManaged() ||
	    !SupportsPooling())
		return false;
	PoolOwner = Pool;
	ResetForPool();
	return true;
}

bool ALSBasicProjectile::ActivateFromPool(ULSProjectilePoolSubsystem* Pool, uint64 Serial,
    const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator)
{
	if (!IsValid(Pool) || PoolOwner.Get() != Pool || ActivationSerial != 0 ||
	    !Pool->IsAcquired(this, Serial))
		return false;
	ActivationSerial = Serial;
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);
	return IsValid(this) && !IsActorBeingDestroyed() && Pool->IsAcquired(this, Serial) && ActivationSerial == Serial;
}

bool ALSBasicProjectile::DeactivateForPool(ULSProjectilePoolSubsystem* Pool, uint64 Serial)
{
	if (!IsValid(Pool) || PoolOwner.Get() != Pool || Serial == 0 || ActivationSerial != Serial ||
	    Pool->IsAcquired(this, Serial))
		return false;
	ActivationSerial = 0;
	ResetForPool();
	return true;
}

bool ALSBasicProjectile::IsCurrentFlight(uint64 Serial) const
{
	return bLaunched && (!IsPoolManaged() || (Serial != 0 && Serial == ActivationSerial &&
	    PoolOwner->IsAcquired(this, Serial)));
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
	if (bLaunched || (IsPoolManaged() && !PoolOwner->IsAcquired(this, ActivationSerial)) ||
	    Direction.ContainsNaN() || Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0 ||
	    !FMath::IsFinite(Radius) || Radius <= 0 || !FMath::IsFinite(Lifetime) || Lifetime <= 0 ||
	    !FMath::IsFinite(Request.DamageAmount) || Request.DamageAmount < 0 ||
	    (Request.bHasSourceFaction && !LSFaction::IsValid(Request.SourceFaction)))
		return false;
	ShotRequest = Request;
	OriginalSource = Request.SourceActor;
	SourceAttack = IsValid(Request.SourceActor) ? Request.SourceActor->FindComponentByClass<ULSBasicAttackComponent>() : nullptr;
	bHasSourceLife = SourceAttack.IsValid();
	SourceLifeSerial = bHasSourceLife ? SourceAttack->GetCombatLifeSerial() : 0;
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
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);
	if (IsPoolManaged())
	{
		const uint64 Serial = ActivationSerial;
		GetWorldTimerManager().SetTimer(PoolLifetimeTimer, FTimerDelegate::CreateWeakLambda(this, [this, Serial]()
		{
			if (IsCurrentFlight(Serial))
				FinishFlight();
		}), Lifetime, false);
	}
	else
		SetLifeSpan(Lifetime);
	if (!Material)
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (Material)
	{
		if (FlightMaterial != Material || !FlightMaterialInstance)
		{
			FlightMaterial = Material;
			FlightMaterialInstance = UMaterialInstanceDynamic::Create(Material, this);
		}
		const FLinearColor Color = VisualStyle == ELSProjectileVisualStyle::TurretBolt
		    ? FLinearColor(1.0f, 0.28f, 0.015f)
		    : (Request.SourceFaction == ELSFaction::Ally ? FLinearColor(0.08f, 0.55f, 1.0f)
		                                             : FLinearColor(0.95f, 0.12f, 0.05f));
		FlightMaterialInstance->SetVectorParameterValue(TEXT("TeamColor"), Color);
		Shaft->SetMaterial(0, FlightMaterialInstance);
		Tip->SetMaterial(0, FlightMaterialInstance);
	}
	else
	{
		Shaft->SetMaterial(0, nullptr);
		Tip->SetMaterial(0, nullptr);
	}
	return true;
}

void ALSBasicProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (IsPoolManaged() && !bLaunched)
		SetActorTickEnabled(false);
	if (!bLaunched && !IsPoolManaged())
	{
		Destroy();
	}
}

void ALSBasicProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PoolLifetimeTimer);
	ActivationSerial = 0;
	bLaunched = false;
	OnDamageApplied.Clear();
	OnDamageAppliedNative.Clear();
	Super::EndPlay(EndPlayReason);
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
		if (AActor* Source = MakeHitRequest().SourceActor)
			Query.AddIgnoredActor(Source);
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
		FinishFlight();
	}
}

FLSDamageRequest ALSBasicProjectile::MakeHitRequest() const
{
	FLSDamageRequest Request = ShotRequest;
	AActor* Source = OriginalSource.Get();
	Request.SourceActor = IsValid(Source) && !Source->IsActorBeingDestroyed() ? Source : nullptr;
	if (bHasSourceLife && (!SourceAttack.IsValid() || SourceAttack->GetCombatLifeSerial() != SourceLifeSerial))
		Request.SourceActor = nullptr;
	return Request;
}

bool ALSBasicProjectile::FindFirstHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBasicProjectile), false, this);
	const FLSDamageRequest Request = MakeHitRequest();
	if (Request.SourceActor)
		Query.AddIgnoredActor(Request.SourceActor);
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
		// 전투 몸체의 통과·명중은 위의 피해 정책이 정한다. 같은 진영과 시체를 지형으로 다시 막지 않는다.
		if (IsValid(Actor) && Cast<ILSFactionInterface>(Actor) && Actor->FindComponentByClass<ULSHealthComponent>())
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
	const uint64 ShotSerial = ActivationSerial;
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
		    IsCurrentFlight(ShotSerial) && bResolved && Result.AppliedDamage > 0)
		{
			OnDamageAppliedNative.Broadcast(IsValid(Target) ? Target : nullptr, Result.AppliedDamage);
			if (IsValid(this) && !IsActorBeingDestroyed() && IsCurrentFlight(ShotSerial) && bResolved)
				OnDamageApplied.Broadcast(IsValid(Target) ? Target : nullptr, Result.AppliedDamage);
		}
	}
	if (IsValid(this) && !IsActorBeingDestroyed() && IsCurrentFlight(ShotSerial) && bResolved)
	{
		FinishFlight();
	}
}

void ALSBasicProjectile::LifeSpanExpired()
{
	FinishFlight();
}

void ALSBasicProjectile::FinishFlight()
{
	if (IsPoolManaged() && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		PoolOwner->ReleaseProjectile({this, ActivationSerial});
		return;
	}
	Destroy();
}

void ALSBasicProjectile::ResetForPool()
{
	SetLifeSpan(0);
	GetWorldTimerManager().ClearTimer(PoolLifetimeTimer);
	SetActorTickEnabled(false);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	OnDamageApplied.Clear();
	OnDamageAppliedNative.Clear();
	ShotRequest = FLSDamageRequest();
	OriginalSource.Reset();
	SourceAttack.Reset();
	SourceLifeSerial = 0;
	bHasSourceLife = false;
	FlightVelocity = FVector::ZeroVector;
	RemainingFlightTime = 0;
	bLaunched = false;
	bResolved = false;
	bHitsFirstBlocking = false;
	bStopsAtWorldGeometry = false;
	FlightOrigin = FVector::ZeroVector;
	VisualStyle = ELSProjectileVisualStyle::Arrow;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	const ALSBasicProjectile* Defaults = GetClass()->GetDefaultObject<ALSBasicProjectile>();
	Shaft->SetRelativeTransform(Defaults->Shaft->GetRelativeTransform());
	Tip->SetRelativeTransform(Defaults->Tip->GetRelativeTransform());
	Collision->SetSphereRadius(Defaults->Collision->GetUnscaledSphereRadius());
	SetActorTransform(FTransform::Identity, false, nullptr, ETeleportType::TeleportPhysics);
}
