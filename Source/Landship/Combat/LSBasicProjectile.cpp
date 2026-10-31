#include "Combat/LSBasicProjectile.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
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
	Shaft->SetStaticMesh(Cube.Object);
	Shaft->SetRelativeLocation(FVector(-35, 0, 0));
	Shaft->SetRelativeScale3D(FVector(0.5f, 0.05f, 0.05f));
	Shaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shaft->SetCanEverAffectNavigation(false);
	Tip = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tip"));
	Tip->SetupAttachment(Collision);
	Tip->SetStaticMesh(Cone.Object);
	Tip->SetRelativeLocation(FVector(-10, 0, 0));
	Tip->SetRelativeRotation(FRotator(90, 0, 0));
	Tip->SetRelativeScale3D(FVector(0.13f, 0.13f, 0.2f));
	Tip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tip->SetCanEverAffectNavigation(false);
}

bool ALSBasicProjectile::Launch(const FLSDamageRequest& Request, AActor* Target, const FVector& Direction, float Speed,
                                float Radius, float Lifetime, UMaterialInterface* Material)
{
	if (bLaunched || !IsValid(Target) || !ULSCombatTargetLibrary::IsCombatAlive(Target) || Direction.ContainsNaN() ||
	    Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0 || !FMath::IsFinite(Radius) || Radius <= 0 ||
	    !FMath::IsFinite(Lifetime) || Lifetime <= 0 || !FMath::IsFinite(Request.DamageAmount) ||
	    Request.DamageAmount < 0 || (Request.bHasSourceFaction && !LSFaction::IsValid(Request.SourceFaction)))
	{
		return false;
	}
	ShotRequest = Request;
	OriginalSource = Request.SourceActor;
	IntendedTarget = Target;
	// 공격자를 강하게 보유하지 않는다. 제거된 뒤에도 저장된 진영으로 오폭을 판정한다.
	ShotRequest.SourceActor = nullptr;
	ShotRequest.DamageCauser = nullptr;
	FlightVelocity = Direction.GetSafeNormal() * Speed;
	Collision->SetSphereRadius(Radius);
	SetActorRotation(FlightVelocity.Rotation());
	RemainingFlightTime = Lifetime;
	bLaunched = true;
	SetLifeSpan(Lifetime);
	if (Material)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		const FLinearColor Color = Request.SourceFaction == ELSFaction::Ally ? FLinearColor(0.08f, 0.55f, 1.0f)
		                                                                     : FLinearColor(0.95f, 0.12f, 0.05f);
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
	// 다른 유닛·건물·지형의 차단 없이 발사 시 지정한 대상의 충돌 형상만 검사한다.
	AActor* Target = IntendedTarget.Get();
	if (IsValid(Target) && !Target->IsActorBeingDestroyed() && ULSCombatTargetLibrary::IsCombatAlive(Target))
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(Target);
		FHitResult ClosestHit;
		bool bHitTarget = false;
		for (UPrimitiveComponent* Component : Components)
		{
			FHitResult Hit;
			if (IsValid(Component) && Component->IsQueryCollisionEnabled() &&
			    Component->SweepComponent(Hit, Start, End, FQuat::Identity,
			                              FCollisionShape::MakeSphere(Collision->GetScaledSphereRadius())) &&
			    (!bHitTarget || Hit.Time < ClosestHit.Time))
			{
				ClosestHit = Hit;
				bHitTarget = true;
			}
		}
		if (bHitTarget)
		{
			ResolveHit(ClosestHit);
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
	if (Health)
	{
		FLSDamageRequest Request = ShotRequest;
		Request.SourceActor = OriginalSource.Get();
		if (!IsValid(Request.SourceActor) || Request.SourceActor->IsActorBeingDestroyed())
		{
			Request.SourceActor = nullptr;
		}
		Request.DamageCauser = this;
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
