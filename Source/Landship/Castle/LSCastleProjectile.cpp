#include "Castle/LSCastleProjectile.h"

#include "Combat/LSCollisionChannels.h"
#include "Obstacles/LSPathObstacle.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Components/PrimitiveComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSUnit.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

ALSCastleProjectile::ALSCastleProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool ALSCastleProjectile::Launch(ELSCastleWeapon Weapon, const FLSCastleWeaponSettings& Settings,
                                 const FLSDamageRequest& Request, const FVector& Aim, bool bFirstBlocking)
{
	if (bLaunched || (Weapon != ELSCastleWeapon::Cannon && Weapon != ELSCastleWeapon::Shotgun) ||
	    !ULSCastleWeaponData::AreSettingsValid(Weapon, Settings) || Aim.ContainsNaN() ||
	    GetActorLocation().ContainsNaN() || !FMath::IsFinite(Request.DamageAmount) || Request.DamageAmount < 0 ||
	    !Request.bHasSourceFaction || !LSFaction::IsValid(Request.SourceFaction))
		return false;
	bHitsFirstBlocking = bFirstBlocking;
	Kind = Weapon;
	ShotSettings = Settings;
	ShotRequest = Request;
	Source = Request.SourceActor;
	ShotRequest.SourceActor = nullptr;
	ShotRequest.DamageCauser = nullptr;
	LaunchLocation = GetActorLocation();
	TargetLocation = Aim;
	const FVector Direction = bHitsFirstBlocking ? (Aim - LaunchLocation).GetSafeNormal()
	                                            : (Aim - LaunchLocation).GetSafeNormal2D();
	if (Weapon == ELSCastleWeapon::Shotgun && Direction.IsNearlyZero())
		return false;
	FlightTime =
	    Weapon == ELSCastleWeapon::Cannon
	        ? FMath::Max(0.2f, static_cast<float>(FVector::Dist2D(LaunchLocation, Aim)) / Settings.ProjectileSpeed)
	        : Settings.Range / Settings.ProjectileSpeed;
	if (!FMath::IsFinite(FlightTime) || FlightTime <= 0)
		return false;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(
	    nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (!Mesh || !Material)
		return false;
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
	Instance->SetVectorParameterValue(TEXT("TeamColor"), Weapon == ELSCastleWeapon::Cannon
	                                                         ? FLinearColor(1.0f, 0.35f, 0.04f)
	                                                         : FLinearColor(1.0f, 0.85f, 0.2f));
	const int32 Count = Weapon == ELSCastleWeapon::Cannon ? 1 : Settings.PelletCount;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Bullet = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Bullet);
		Bullet->SetupAttachment(RootComponent);
		Bullet->SetStaticMesh(Mesh);
		Bullet->SetMaterial(0, Instance);
		Bullet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bullet->SetCanEverAffectNavigation(false);
		const float Radius = Weapon == ELSCastleWeapon::Cannon ? Settings.ShellRadius : Settings.PelletRadius;
		Bullet->SetRelativeScale3D(FVector(Radius / 50.0f));
		Bullet->RegisterComponent();
		Bullets.Add(Bullet);
		const float Angle = Count == 1 ? 0 : -Settings.ConeAngle / 2 + Settings.ConeAngle * Index / (Count - 1);
		Directions.Add(Direction.RotateAngleAxis(Angle, FVector::UpVector));
		ActivePellets.Add(true);
	}
	bLaunched = true;
	// 큰 프레임에서도 착탄 처리가 수명 타이머보다 먼저 실행되도록 Tick에서 수명을 끝낸다.
	return true;
}

void ALSCastleProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (!bLaunched)
		Destroy();
}

bool ALSCastleProjectile::IsLivingCombatant(AActor* Target) const
{
	const ILSFactionInterface* Faction = IsValid(Target) ? Cast<ILSFactionInterface>(Target) : nullptr;
	return Faction && !Target->IsActorBeingDestroyed() && ULSCombatTargetLibrary::IsCombatAlive(Target) &&
	       LSFaction::IsValid(Faction->GetFaction());
}

FLSDamageRequest ALSCastleProjectile::MakeHitRequest() const
{
	FLSDamageRequest Request = ShotRequest;
	Request.SourceActor = Source.IsValid() && !Source->IsActorBeingDestroyed() ? Source.Get() : nullptr;
	return Request;
}

bool ALSCastleProjectile::FindPelletHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	if (bHitsFirstBlocking)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSObstaclePellet), false, this);
		Query.bFindInitialOverlaps = true;
		if (Source.IsValid())
			Query.AddIgnoredActor(Source.Get());
		return GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity,
		    LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(ShotSettings.PelletRadius), Query) &&
		    (OutHit.bBlockingHit || OutHit.bStartPenetrating) && IsValid(OutHit.GetComponent());
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShotgunPellet), false, this);
	if (Source.IsValid())
		Query.AddIgnoredActor(Source.Get());
	const FLSDamageRequest Request = MakeHitRequest();
	bool bSkipped;
	do
	{
		bSkipped = false;
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
		                                   FCollisionShape::MakeSphere(ShotSettings.PelletRadius), Query);
		Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
		for (const FHitResult& Hit : Hits)
		{
			if (IsLivingCombatant(Hit.GetActor()) &&
			    ULSHealthComponent::EvaluateDamagePolicy(Request, Hit.GetActor()) == ELSApplyDamageStatus::Applied)
			{
				OutHit = Hit;
				return true;
			}
			if (IsValid(Hit.GetActor()))
			{
				Query.AddIgnoredActor(Hit.GetActor());
				bSkipped = true;
			}
		}
	} while (bSkipped);
	return false;
}

void ALSCastleProjectile::ApplyHit(AActor* Target, float Damage, const FVector& Direction, bool bKnockback)
{
	if (!IsLivingCombatant(Target))
		return;
	ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	if (!Health)
		return;
	FLSDamageRequest Request = MakeHitRequest();
	Request.DamageAmount = Damage;
	Request.DamageCauser = this;
	const FLSApplyDamageResult Result = Health->ApplyDamage(Request);
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
		return;
	if (bKnockback && Result.AppliedDamage > 0 && IsLivingCombatant(Target) && !PushedTargets.Contains(Target))
	{
		if (ALSUnit* Unit = Cast<ALSUnit>(Target); Unit && Unit->Selection->Kind == ELSSelectionKind::Unit)
		{
			PushedTargets.Add(Target);
			Unit->Orders->ApplyKnockback(Direction * ShotSettings.KnockbackSpeed +
			                             FVector(0, 0, ShotSettings.KnockbackLift));
		}
	}
}

void ALSCastleProjectile::Detonate()
{
	bResolved = true;
	SetActorTickEnabled(false);
	TArray<TWeakObjectPtr<AActor>> Targets;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!IsLivingCombatant(*It))
			continue;
		double Distance = FVector::Dist2D(It->GetActorLocation(), TargetLocation);
		if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(*It))
		{
			Distance = LSObstacleGeometry::GetPlanarBodyDistance(Obstacle->GetObstacleBody(), TargetLocation);
		}
		if (Distance <= ShotSettings.Radius)
			Targets.Add(*It);
	}
	for (const TWeakObjectPtr<AActor>& Target : Targets)
	{
		if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
			return;
		ApplyHit(Target.Get(), ShotSettings.Damage, FVector::ZeroVector, false);
	}
	if (IsValid(this) && !IsActorBeingDestroyed())
		Destroy();
}

void ALSCastleProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLaunched || bResolved || !GetWorld() || GetWorld()->bIsTearingDown || !FMath::IsFinite(DeltaSeconds) ||
	    DeltaSeconds <= 0)
		return;
	Elapsed = FMath::Min(FlightTime, Elapsed + DeltaSeconds);
	if (Kind == ELSCastleWeapon::Cannon)
	{
		const float T = Elapsed / FlightTime;
		const FVector Position =
		    FMath::Lerp(LaunchLocation, TargetLocation, T) + FVector(0, 0, 4.0 * ShotSettings.ArcHeight * T * (1 - T));
		SetActorLocation(Position, false);
		if (Elapsed >= FlightTime)
			Detonate();
		return;
	}
	bool bAnyActive = false;
	for (int32 Index = 0; Index < Bullets.Num(); ++Index)
	{
		if (!ActivePellets[Index])
			continue;
		if (!IsValid(Bullets[Index]))
		{
			ActivePellets[Index] = false;
			continue;
		}
		const FVector End = LaunchLocation + Directions[Index] * ShotSettings.ProjectileSpeed * Elapsed;
		FHitResult Hit;
		if (FindPelletHit(Bullets[Index]->GetComponentLocation(), End, Hit))
		{
			ActivePellets[Index] = false;
			Bullets[Index]->SetVisibility(false);
			ApplyHit(Hit.GetActor(), ShotSettings.Damage / ShotSettings.PelletCount,
			         bHitsFirstBlocking ? Directions[Index].GetSafeNormal2D()
			                          : (Hit.GetActor()->GetActorLocation() - LaunchLocation).GetSafeNormal2D(), true);
			if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
				return;
		}
		else
		{
			Bullets[Index]->SetWorldLocation(End, false);
			bAnyActive = true;
		}
	}
	if (!bAnyActive || Elapsed >= FlightTime)
	{
		bResolved = true;
		Destroy();
	}
}
