#include "Match/LSAutoBattleGameMode.h"

#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Castle/LSCastleProjectile.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Match/LSAutoBattleCastle.h"

namespace
{
FLinearColor GetCastleFieldColor(ELSCastleField Field)
{
	return Field == ELSCastleField::Swamp ? FLinearColor(0.48f, 0.12f, 0.67f) : FLinearColor(0.08f, 0.75f, 0.16f);
}
}

float ALSAutoBattleGameMode::GetFieldCooldown(ELSCastleField Field) const
{
	const uint8 Index = static_cast<uint8>(Field);
	return Index < UE_ARRAY_COUNT(CastleFields) && GetWorld()
	    ? static_cast<float>(FMath::Max(0.0, CastleFields[Index].ReadyAt - GetWorld()->GetTimeSeconds())) : 0.0f;
}

ELSWeaponCastResult ALSAutoBattleGameMode::GetFieldStatus(ELSCastleField Field) const
{
	if (static_cast<uint8>(Field) >= UE_ARRAY_COUNT(CastleFields))
		return ELSWeaponCastResult::InvalidData;
	if (!FMath::IsFinite(FieldRadius) || FieldRadius <= 0 || !FMath::IsFinite(FieldCastRange) || FieldCastRange <= 0 ||
	    !FMath::IsFinite(FieldDuration) || FieldDuration <= 0 || !FMath::IsFinite(FieldCooldown) || FieldCooldown <= 0 ||
	    !FMath::IsFinite(SwampSpeed) || SwampSpeed <= 0 || SwampSpeed > 1 ||
	    !FMath::IsFinite(StimSpeed) || StimSpeed < 1)
		return ELSWeaponCastResult::InvalidData;
	if (Phase != ELSAutoBattlePhase::Fighting || !ULSCombatTargetLibrary::IsCombatAlive(AllyCastle.Get()))
		return ELSWeaponCastResult::Unavailable;
	if (Field == ELSCastleField::Wall &&
	    (!FMath::IsFinite(WallDuration) || WallDuration <= 0 || WallHalfExtent.ContainsNaN() ||
	     WallHalfExtent.GetMin() <= 0))
		return ELSWeaponCastResult::InvalidData;
	if (Field == ELSCastleField::Wall && SummonedWall.IsValid())
		return ELSWeaponCastResult::Cooldown;
	if (Field != ELSCastleField::Wall)
	{
		if (!FieldDecalMaterial)
			return ELSWeaponCastResult::InvalidData;
		if (CastleFields[static_cast<uint8>(Field)].Projectile.IsValid())
			return ELSWeaponCastResult::Busy;
	}
	return GetFieldCooldown(Field) > 0 ? ELSWeaponCastResult::Cooldown : ELSWeaponCastResult::Ready;
}

ELSWeaponCastResult ALSAutoBattleGameMode::ValidateFieldAim(ELSCastleField Field, const FVector& Aim) const
{
	const ELSWeaponCastResult Status = GetFieldStatus(Field);
	if (Status != ELSWeaponCastResult::Ready)
		return Status;
	if (!Scenario || Aim.ContainsNaN() ||
	    !FBox2D(Scenario->BoundsMin, Scenario->BoundsMax).IsInsideOrOn(FVector2D(Aim)))
		return ELSWeaponCastResult::InvalidAim;
	if (FVector::DistSquared2D(AllyCastle->GetActorLocation(), Aim) > FMath::Square(FieldCastRange))
		return ELSWeaponCastResult::OutOfRange;
	if (Field == ELSCastleField::Wall)
	{
		FTransform Transform;
		FVector Extent;
		if (!GetWallPreview(Aim, Transform, Extent))
			return ELSWeaponCastResult::InvalidAim;
		FCollisionObjectQueryParams Ground;
		Ground.AddObjectTypesToQuery(LSCollisionChannels::Ground);
		Ground.AddObjectTypesToQuery(ECC_WorldStatic);
		FHitResult Support;
		if (!GetWorld()->LineTraceSingleByObjectType(Support, Aim + FVector(0, 0, 20),
		                                            Aim - FVector(0, 0, 20), Ground) ||
		    Support.ImpactNormal.Z < 0.7)
			return ELSWeaponCastResult::InvalidAim;
		const FBox2D Bounds(Scenario->BoundsMin, Scenario->BoundsMax);
		for (const double X : {-Extent.X, Extent.X})
		{
			for (const double Y : {-Extent.Y, Extent.Y})
			{
				if (!Bounds.IsInsideOrOn(FVector2D(Transform.TransformPosition(FVector(X, Y, 0)))))
					return ELSWeaponCastResult::InvalidAim;
			}
		}
		FCollisionObjectQueryParams Blockers;
		Blockers.AddObjectTypesToQuery(LSCollisionChannels::Unit);
		Blockers.AddObjectTypesToQuery(LSCollisionChannels::Building);
		Blockers.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
		Blockers.AddObjectTypesToQuery(ECC_WorldStatic);
		Blockers.AddObjectTypesToQuery(ECC_WorldDynamic);
		FCollisionQueryParams OccupancyQuery(SCENE_QUERY_STAT(LSCastleWallPlacement));
		if (const UPrimitiveComponent* SupportComponent = Support.GetComponent())
			OccupancyQuery.AddIgnoredComponent(SupportComponent);
		if (GetWorld()->OverlapAnyTestByObjectType(Transform.GetLocation(), Transform.GetRotation(), Blockers,
		                                           FCollisionShape::MakeBox(Extent), OccupancyQuery))
			return ELSWeaponCastResult::InvalidAim;
	}
	return ELSWeaponCastResult::Ready;
}

bool ALSAutoBattleGameMode::GetWallPreview(const FVector& Aim, FTransform& OutTransform, FVector& OutExtent) const
{
	if (!IsValid(AllyCastle.Get()) || Aim.ContainsNaN())
		return false;
	const FVector Direction = (Aim - AllyCastle->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		return false;
	OutExtent = WallHalfExtent;
	OutTransform = FTransform(FRotator(0, Direction.Rotation().Yaw + 90, 0),
	                          Aim + FVector(0, 0, OutExtent.Z));
	return true;
}

bool ALSAutoBattleGameMode::SpawnCastleWall(const FTransform& Transform, const FVector& Extent)
{
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
		return false;
	FTransform MeshTransform = Transform;
	MeshTransform.SetScale3D(Extent / 50.0);
	auto* Wall = GetWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), MeshTransform,
	    this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Wall)
		return false;
	auto* Mesh = Wall->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionObjectType(LSCollisionChannels::PathObstacle);
	Mesh->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Mesh->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	Mesh->SetCanEverAffectNavigation(true);
	Wall->FinishSpawning(MeshTransform);
	if (!IsValid(Wall) || Wall->IsActorBeingDestroyed())
		return false;
	Wall->SetLifeSpan(WallDuration);
	SummonedWall = Wall;
	return true;
}

ELSWeaponCastResult ALSAutoBattleGameMode::CastField(ELSCastleField Field, const FVector& Aim)
{
	if (bChanging)
		return ELSWeaponCastResult::Busy;
	const ELSWeaponCastResult Status = ValidateFieldAim(Field, Aim);
	if (Status != ELSWeaponCastResult::Ready)
		return Status;

	const double Now = GetWorld()->GetTimeSeconds();
	if (Field == ELSCastleField::Wall)
	{
		FTransform Transform;
		FVector Extent;
		TGuardValue<bool> Guard(bChanging, true);
		if (!GetWallPreview(Aim, Transform, Extent) || !SpawnCastleWall(Transform, Extent))
			return ELSWeaponCastResult::Unavailable;
		CastleFields[static_cast<uint8>(Field)].ReadyAt = Now + FieldCooldown;
		return ELSWeaponCastResult::Ready;
	}
	TGuardValue<bool> Guard(bChanging, true);
	return LaunchCastleField(Field, Aim) ? ELSWeaponCastResult::Ready : ELSWeaponCastResult::Unavailable;
}

bool ALSAutoBattleGameMode::LaunchCastleField(ELSCastleField Field, const FVector& Aim)
{
	FLSCastleWeaponSettings Settings;
	if (!AllyCastle->Weapons->GetResolvedSettings(ELSCastleWeapon::Cannon, Settings))
		return false;
	Settings.Range = FieldCastRange;
	Settings.Damage = 0;
	const FVector Origin = AllyCastle->Weapons->GetWeaponFireOrigin(ELSCastleWeapon::Cannon, Aim);
	const FTransform Transform((Aim - Origin).Rotation(), Origin);
	auto* Projectile = GetWorld()->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(),
	    Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile)
		return false;
	const auto DamageRequest = ULSHealthComponent::MakeDamageRequest(0, AllyCastle.Get(), nullptr);
	auto Impact = FLSCastlePayloadImpact::CreateUObject(this, &ThisClass::OnCastleFieldImpact, Field, BattleId);
	if (!Projectile->LaunchPayload(Settings, DamageRequest, Aim, GetCastleFieldColor(Field), MoveTemp(Impact)))
	{
		Projectile->Destroy();
		return false;
	}
	Projectile->FinishSpawning(Transform);
	if (!IsValid(Projectile) || Projectile->IsActorBeingDestroyed())
		return false;

	auto& State = CastleFields[static_cast<uint8>(Field)];
	State.Projectile = Projectile;
	State.ReadyAt = GetWorld()->GetTimeSeconds() + FieldCooldown;
	return true;
}

void ALSAutoBattleGameMode::OnCastleFieldImpact(const FVector& Impact, ELSCastleField Field, int32 SourceBattleId)
{
	if (Phase != ELSAutoBattlePhase::Fighting || BattleId != SourceBattleId || GetWorld()->bIsTearingDown)
		return;
	auto& State = CastleFields[static_cast<uint8>(Field)];
	State.Projectile.Reset();
	FCollisionObjectQueryParams Ground;
	Ground.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	Ground.AddObjectTypesToQuery(ECC_WorldStatic);
	FHitResult Surface;
	if (!GetWorld()->LineTraceSingleByObjectType(Surface, Impact + FVector(0, 0, 40),
	                                            Impact - FVector(0, 0, 500), Ground) ||
	    Surface.ImpactNormal.Z < 0.7)
		return;

	if (State.Decal.IsValid())
		State.Decal->DestroyComponent();
	State.Center = Surface.ImpactPoint;
	State.Expires = GetWorld()->GetTimeSeconds() + FieldDuration;
	auto* Decal = UGameplayStatics::SpawnDecalAtLocation(this, FieldDecalMaterial, FVector(240, FieldRadius, FieldRadius),
	    State.Center + FVector(0, 0, 2), FRotator(-90, 0, 0), FieldDuration);
	if (Decal)
	{
		if (auto* Material = Decal->CreateDynamicMaterialInstance())
			Material->SetVectorParameterValue(TEXT("FieldColor"), GetCastleFieldColor(Field));
		State.Decal = Decal;
	}
	UpdateCastleFields();
}

void ALSAutoBattleGameMode::UpdateCastleFields()
{
	const double Now = GetWorld()->GetTimeSeconds();
	auto& Swamp = CastleFields[static_cast<uint8>(ELSCastleField::Swamp)];
	auto& Stim = CastleFields[static_cast<uint8>(ELSCastleField::Stim)];
	if (Swamp.Expires == 0 && Stim.Expires == 0)
		return;
	const auto Contains = [this, Now](const FLSCastleFieldState& Field, const ALSTroopUnit& Unit)
	{
		const FVector Position = Unit.GetActorLocation() - FVector(0, 0, Unit.GetSimpleCollisionHalfHeight());
		return Field.Expires > Now && FMath::Abs(Position.Z - Field.Center.Z) <= 240.0 &&
		       FVector::DistSquared2D(Position, Field.Center) <= FMath::Square(FieldRadius);
	};
	for (ALSUnit* Unit : Units)
	{
		if (auto* Troop = Cast<ALSTroopUnit>(Unit); ULSCombatTargetLibrary::IsCombatAlive(Troop))
		{
			const bool bAlly = Troop->GetFaction() == ELSFaction::Ally;
			const auto& Field = bAlly ? Stim : Swamp;
			Troop->SetFieldSpeedMultiplier(Contains(Field, *Troop) ? (bAlly ? StimSpeed : SwampSpeed) : 1.0f);
		}
	}

	if (Swamp.Expires <= Now)
		Swamp.Expires = 0;
	if (Stim.Expires <= Now)
		Stim.Expires = 0;
}

void ALSAutoBattleGameMode::ClearCastleFields()
{
	if (SummonedWall.IsValid())
		SummonedWall->Destroy();
	SummonedWall.Reset();
	for (auto& Field : CastleFields)
	{
		if (Field.Projectile.IsValid())
			Field.Projectile->Destroy();
		if (Field.Decal.IsValid())
			Field.Decal->DestroyComponent();
		Field = {};
	}
	for (ALSUnit* Unit : Units)
	{
		if (auto* Troop = Cast<ALSTroopUnit>(Unit); IsValid(Troop))
			Troop->SetFieldSpeedMultiplier(1.0f);
	}
}
