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
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
bool IsCannonCover(const FHitResult& Hit)
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	return IsValid(Component) && Component->GetCollisionObjectType() != LSCollisionChannels::Unit &&
	       !Cast<APawn>(Component->GetOwner());
}
}

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
	if (Weapon == ELSCastleWeapon::Cannon && !ULSCastleWeaponData::TryGetCannonArcSteps(Settings, CannonArcSteps))
		return false;
	bHitsFirstBlocking = bFirstBlocking;
	Kind = Weapon;
	ShotSettings = Settings;
	ShotRequest = Request;
	Source = Request.SourceActor;
	ShotRequest.SourceActor = nullptr;
	ShotRequest.DamageCauser = nullptr;
	LaunchLocation = GetActorLocation();
	PreviousFlightPosition = LaunchLocation;
	ShotRequest.bHasAttackOrigin = true;
	ShotRequest.AttackOrigin = LaunchLocation;
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
		// SetStaticMesh는 등록 전에도 내비게이션 형상을 갱신한다. 시각용 탄은 그 전에 제외한다.
		Bullet->SetCanEverAffectNavigation(false);
		Bullet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bullet->SetStaticMesh(Mesh);
		Bullet->SetMaterial(0, Instance);
		const float Radius = Weapon == ELSCastleWeapon::Cannon ? Settings.ShellRadius : Settings.PelletRadius;
		Bullet->SetRelativeScale3D(FVector(Radius / 50.0f));
		Bullet->RegisterComponent();
		Bullets.Add(Bullet);
		const float Angle = Count == 1 ? 0 : -Settings.ConeAngle / 2 + Settings.ConeAngle * Index / (Count - 1);
		Directions.Add(Direction.RotateAngleAxis(Angle, FVector::UpVector));
		ActivePellets.Add(true);
		PreviousPelletPositions.Add(LaunchLocation);
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
	bool bHasDamageHit = false;
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
				bHasDamageHit = true;
				break;
			}
			if (IsValid(Hit.GetActor()))
			{
				Query.AddIgnoredActor(Hit.GetActor());
				bSkipped = true;
			}
		}
	} while (bSkipped && !bHasDamageHit);
	FCollisionQueryParams GeometryQuery(SCENE_QUERY_STAT(LSShotgunCover), false, this);
	GeometryQuery.bFindInitialOverlaps = true;
	if (Source.IsValid())
		GeometryQuery.AddIgnoredActor(Source.Get());
	FHitResult GeometryHit;
	while (GetWorld()->SweepSingleByChannel(GeometryHit, Start, End, FQuat::Identity,
		LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(ShotSettings.PelletRadius), GeometryQuery) &&
		(GeometryHit.bBlockingHit || GeometryHit.bStartPenetrating))
	{
		AActor* Actor = GeometryHit.GetActor();
		// 전투 몸체의 명중·통과는 위의 피해 정책이 정한다. 아군이나 시체를 벽으로 다시 막지 않는다.
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

FVector ALSCastleProjectile::GetCannonPosition(float NormalizedTime) const
{
	return FMath::Lerp(LaunchLocation, TargetLocation, NormalizedTime) +
	       FVector(0, 0, 4.0 * ShotSettings.ArcHeight * NormalizedTime * (1 - NormalizedTime));
}

bool ALSCastleProjectile::FindCannonCoverHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCannonCover), false, this);
	Query.bFindInitialOverlaps = true;
	if (Source.IsValid())
		Query.AddIgnoredActor(Source.Get());
	while (GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity,
		LSCollisionChannels::ObstacleProjectile, FCollisionShape::MakeSphere(ShotSettings.ShellRadius), Query) &&
		(OutHit.bBlockingHit || OutHit.bStartPenetrating))
	{
		if (IsCannonCover(OutHit))
			return true;
		if (!IsValid(OutHit.GetComponent()))
			return false;
		Query.AddIgnoredComponent(OutHit.GetComponent());
	}
	return false;
}

bool ALSCastleProjectile::HasCannonBlastLine(AActor* Target) const
{
	FVector Probe = Target->GetActorLocation();
	if (const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		FVector Closest;
		double Distance;
		if (LSObstacleGeometry::GetClosestBodyPoint(Body, TargetLocation, Closest, Distance))
			Probe = Closest;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCannonBlastCover), false, this);
	Query.bFindInitialOverlaps = true;
	Query.AddIgnoredActor(Target);
	const double Length = FVector::Distance(Probe, TargetLocation);
	if (Length <= UE_KINDA_SMALL_NUMBER)
		return true;
	FHitResult Hit;
	// 대상 표면에서 폭발점으로 역추적하면 착탄면에서 바깥으로 나가는 광선의 초기 겹침을 피한다.
	// 폭발점 그 자체의 접촉은 차폐 두께가 아니며, 그 전에 만난 인접한 벽은 그대로 막는다.
	while (GetWorld()->LineTraceSingleByChannel(Hit, Probe, TargetLocation, LSCollisionChannels::ExplosionOcclusion, Query))
	{
		if (Hit.Distance >= Length - UE_KINDA_SMALL_NUMBER)
			return true;
		if (IsCannonCover(Hit))
			return false;
		if (!IsValid(Hit.GetComponent()))
			return false;
		Query.AddIgnoredComponent(Hit.GetComponent());
	}
	return true;
}

void ALSCastleProjectile::ApplyHit(AActor* Target, float Damage, const FVector& Direction, bool bKnockback,
                                 const FVector& Impact, const FVector& IncomingDirection)
{
	if (!IsLivingCombatant(Target))
		return;
	ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
	if (!Health)
		return;
	FLSDamageRequest Request = MakeHitRequest();
	Request.DamageAmount = Damage;
	Request.DamageCauser = this;
	Request.bHasImpactLocation = true;
	Request.ImpactLocation = Impact;
	Request.bHasIncomingDirection = !IncomingDirection.IsNearlyZero();
	Request.IncomingDirection = IncomingDirection.GetSafeNormal();
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
	struct FLSCannonBlastTarget
	{
		TWeakObjectPtr<AActor> Actor;
		FVector KnockbackDirection;
		FVector ImpactLocation;
	};
	TArray<FLSCannonBlastTarget> Targets;
	const FVector IncomingDirection = (TargetLocation - LaunchLocation).GetSafeNormal2D();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!IsLivingCombatant(*It))
			continue;
		double Distance = FVector::Dist2D(It->GetActorLocation(), TargetLocation);
		const UPrimitiveComponent* Body = nullptr;
		if (const ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(*It))
			Body = Obstacle->GetObstacleBody();
		else if (!It->IsA<APawn>())
		{
			const auto* RootBody = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (RootBody && RootBody->GetCollisionObjectType() == LSCollisionChannels::Building)
				Body = RootBody;
		}
		if (Body)
		{
			const double BodyDistance = LSObstacleGeometry::GetPlanarBodyDistance(Body, TargetLocation);
			// 상자·구형인 건물의 실제 외곽을 쓴다. 권위 있는 지원 형상이 없으면 기존 중심 판정을 유지한다.
			if (FMath::IsFinite(BodyDistance))
				Distance = BodyDistance;
		}
		if (Distance <= ShotSettings.Radius && HasCannonBlastLine(*It))
		{
			FVector Direction = (It->GetActorLocation() - TargetLocation).GetSafeNormal2D();
			if (Direction.IsNearlyZero())
				Direction = IncomingDirection;
			// 앞 대상의 피해 알림이 다른 대상을 옮겨도 범위 판정 때의 방향을 유지한다.
			Targets.Add({*It, Direction, It->GetActorLocation()});
		}
	}
	for (const FLSCannonBlastTarget& Target : Targets)
	{
		if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
			return;
		AActor* Actor = Target.Actor.Get();
		if (!IsLivingCombatant(Actor))
			continue;
		const FVector KnockbackVelocity = Target.KnockbackDirection * ShotSettings.KnockbackSpeed +
		                                 FVector(0, 0, ShotSettings.KnockbackLift);
		ApplyHit(Actor, ShotSettings.Damage, Target.KnockbackDirection, !KnockbackVelocity.IsNearlyZero(),
		         Target.ImpactLocation, Target.KnockbackDirection);
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
	const float PreviousElapsed = Elapsed;
	Elapsed = FMath::Min(FlightTime, Elapsed + DeltaSeconds);
	if (Kind == ELSCastleWeapon::Cannon)
	{
		PreviousFlightPosition = GetActorLocation();
		const float StartTime = PreviousElapsed / FlightTime;
		const float EndTime = Elapsed / FlightTime;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(CannonArcSteps * static_cast<double>(EndTime - StartTime)));
		for (int32 Index = 1; Index <= Steps; ++Index)
		{
			const FVector Position = GetCannonPosition(FMath::Lerp(StartTime, EndTime, static_cast<float>(Index) / Steps));
			FHitResult Hit;
			if (FindCannonCoverHit(GetActorLocation(), Position, Hit))
			{
				TargetLocation = Hit.ImpactPoint;
				SetActorLocation(TargetLocation, false);
				Detonate();
				return;
			}
			SetActorLocation(Position, false);
		}
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
		PreviousPelletPositions[Index] = Bullets[Index]->GetComponentLocation();
		FHitResult Hit;
		if (FindPelletHit(Bullets[Index]->GetComponentLocation(), End, Hit))
		{
			ActivePellets[Index] = false;
			Bullets[Index]->SetVisibility(false);
			AActor* Target = Hit.GetActor();
			ApplyHit(Target, ShotSettings.Damage / ShotSettings.PelletCount,
			         bHitsFirstBlocking ? Directions[Index].GetSafeNormal2D()
			                          : IsValid(Target) ? (Target->GetActorLocation() - LaunchLocation).GetSafeNormal2D()
			                                            : Directions[Index].GetSafeNormal2D(), true,
			         Hit.ImpactPoint, Directions[Index]);
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

void ALSCastleProjectile::GetFlightPositions(TArray<FVector>& OutPositions) const
{
	OutPositions.Reset();
	if (!IsInFlight())
		return;
	if (Kind == ELSCastleWeapon::Cannon)
	{
		OutPositions.Add(GetActorLocation());
		return;
	}
	for (int32 Index = 0; Index < Bullets.Num(); ++Index)
	{
		if (ActivePellets[Index] && IsValid(Bullets[Index]))
			OutPositions.Add(Bullets[Index]->GetComponentLocation());
	}
}

void ALSCastleProjectile::GetFlightSegments(TArray<TPair<FVector, FVector>>& OutSegments) const
{
	OutSegments.Reset();
	if (!IsInFlight())
		return;
	if (Kind == ELSCastleWeapon::Cannon)
	{
		OutSegments.Emplace(PreviousFlightPosition, GetActorLocation());
		return;
	}
	for (int32 Index = 0; Index < Bullets.Num(); ++Index)
	{
		if (ActivePellets[Index] && IsValid(Bullets[Index]))
			OutSegments.Emplace(PreviousPelletPositions[Index], Bullets[Index]->GetComponentLocation());
	}
}
