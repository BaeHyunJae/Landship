#include "Castle/LSCastleWeaponComponent.h"

#include "Combat/LSBasicProjectile.h"
#include "Components/PrimitiveComponent.h"
#include "Obstacles/LSPathObstacle.h"
#include "Vision/LSVisionSubsystem.h"
#include "Castle/LSCastleProjectile.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/World.h"
#include "Match/LSBattleOutcomePolicy.h"

ULSCastleWeaponComponent::ULSCastleWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	WeaponDefinitions = {
	    TSoftObjectPtr<ULSCastleWeaponData>(FSoftObjectPath(TEXT("/Game/Landship/Castle/Data/DA_Cannon.DA_Cannon"))),
	    TSoftObjectPtr<ULSCastleWeaponData>(FSoftObjectPath(TEXT("/Game/Landship/Castle/Data/DA_Shotgun.DA_Shotgun"))),
	    TSoftObjectPtr<ULSCastleWeaponData>(
	        FSoftObjectPath(TEXT("/Game/Landship/Castle/Data/DA_Ballista.DA_Ballista")))};
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
		InitialStates.Add(Weapon, FLSCastleWeaponState());
}

bool ULSCastleWeaponComponent::SetWeaponDefinitions(const TArray<ULSCastleWeaponData*>& InDefinitions)
{
	if (HasBegunPlay())
		return false;
	TMap<ELSCastleWeapon, TObjectPtr<ULSCastleWeaponData>> NewDefinitions;
	TArray<TSoftObjectPtr<ULSCastleWeaponData>> References;
	for (ULSCastleWeaponData* Data : InDefinitions)
	{
		if (!IsValid(Data) || !Data->IsDefinitionValid() || NewDefinitions.Contains(Data->Weapon))
			return false;
		NewDefinitions.Add(Data->Weapon, Data);
		References.Add(TSoftObjectPtr<ULSCastleWeaponData>(Data));
	}
	WeaponDefinitions = MoveTemp(References);
	Definitions = MoveTemp(NewDefinitions);
	return true;
}

void ULSCastleWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	TMap<ELSCastleWeapon, TObjectPtr<ULSCastleWeaponData>> Loaded;
	TSet<ELSCastleWeapon> Seen;
	for (const TSoftObjectPtr<ULSCastleWeaponData>& Reference : WeaponDefinitions)
	{
		ULSCastleWeaponData* Data = Reference.LoadSynchronous();
		if (!IsValid(Data))
			continue;
		if (Seen.Contains(Data->Weapon))
		{
			Loaded.Remove(Data->Weapon);
			continue;
		}
		Seen.Add(Data->Weapon);
		if (Data->IsDefinitionValid())
			Loaded.Add(Data->Weapon, Data);
	}
	Definitions = MoveTemp(Loaded);
	States = InitialStates;
	for (auto& Pair : States)
		Pair.Value.ReadyTime = 0;
	if (LSBattleOutcome::AreActionsStopped(GetWorld())) EndBattleActions();
}

void ULSCastleWeaponComponent::EndBattleActions()
{
	bBattleActionsEnded = true;
	LastCastTime = -100;
}

bool ULSCastleWeaponComponent::GetWeaponState(ELSCastleWeapon Weapon, FLSCastleWeaponState& OutState) const
{
	const FLSCastleWeaponState* State = States.Find(Weapon);
	if (!State)
		return false;
	OutState = *State;
	return true;
}

int32 ULSCastleWeaponComponent::GetMaxLevel(ELSCastleWeapon Weapon) const
{
	const TObjectPtr<ULSCastleWeaponData>* Data = Definitions.Find(Weapon);
	return Data && IsValid(Data->Get()) ? (*Data)->Levels.Num() : 0;
}

bool ULSCastleWeaponComponent::GetResolvedSettings(ELSCastleWeapon Weapon, FLSCastleWeaponSettings& OutSettings) const
{
	const FLSCastleWeaponSettings* Settings = FindSettings(Weapon);
	if (!Settings)
		return false;
	OutSettings = *Settings;
	return true;
}

const FLSCastleWeaponSettings* ULSCastleWeaponComponent::FindSettings(ELSCastleWeapon Weapon) const
{
	const FLSCastleWeaponState* State = States.Find(Weapon);
	const TObjectPtr<ULSCastleWeaponData>* Data = Definitions.Find(Weapon);
	return State && Data && IsValid(Data->Get()) && (*Data)->Weapon == Weapon ? (*Data)->FindLevel(State->Level)
	                                                                          : nullptr;
}

ELSWeaponChangeResult ULSCastleWeaponComponent::ValidateChange(ELSCastleWeapon Weapon) const
{
	if (!HasBegunPlay() || !IsActive() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(GetOwner()) ||
	    GetOwner()->IsActorBeingDestroyed() || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
		return ELSWeaponChangeResult::Unavailable;
	if (bCasting || bChangingState)
		return ELSWeaponChangeResult::Busy;
	if (!States.Contains(Weapon))
		return ELSWeaponChangeResult::InvalidWeapon;
	return FindSettings(Weapon) ? ELSWeaponChangeResult::Success : ELSWeaponChangeResult::InvalidData;
}

void ULSCastleWeaponComponent::NotifyStateChanged(ELSCastleWeapon Weapon)
{
	TGuardValue<bool> Guard(bChangingState, true);
	const FLSCastleWeaponState Snapshot = States.FindChecked(Weapon);
	OnWeaponStateChanged.Broadcast(Weapon, Snapshot);
}

ELSWeaponChangeResult ULSCastleWeaponComponent::UnlockWeapon(ELSCastleWeapon Weapon)
{
	return SetWeaponUnlocked(Weapon, true);
}

ELSWeaponChangeResult ULSCastleWeaponComponent::SetWeaponUnlocked(ELSCastleWeapon Weapon, bool bUnlocked)
{
	const ELSWeaponChangeResult Result = ValidateChange(Weapon);
	if (Result != ELSWeaponChangeResult::Success)
		return Result;
	FLSCastleWeaponState& State = States.FindChecked(Weapon);
	if (State.bUnlocked == bUnlocked)
		return ELSWeaponChangeResult::Success;
	State.bUnlocked = bUnlocked;
	NotifyStateChanged(Weapon);
	return ELSWeaponChangeResult::Success;
}

ELSWeaponChangeResult ULSCastleWeaponComponent::SetWeaponEnabled(ELSCastleWeapon Weapon, bool bEnabled)
{
	const ELSWeaponChangeResult Result = ValidateChange(Weapon);
	if (Result != ELSWeaponChangeResult::Success)
		return Result;
	FLSCastleWeaponState& State = States.FindChecked(Weapon);
	if (bEnabled && !State.bUnlocked)
		return ELSWeaponChangeResult::Locked;
	if (State.bEnabled == bEnabled)
		return ELSWeaponChangeResult::Success;
	State.bEnabled = bEnabled;
	NotifyStateChanged(Weapon);
	return ELSWeaponChangeResult::Success;
}

ELSWeaponChangeResult ULSCastleWeaponComponent::UpgradeWeapon(ELSCastleWeapon Weapon)
{
	const ELSWeaponChangeResult Result = ValidateChange(Weapon);
	if (Result != ELSWeaponChangeResult::Success)
		return Result;
	FLSCastleWeaponState& State = States.FindChecked(Weapon);
	if (!State.bUnlocked)
		return ELSWeaponChangeResult::Locked;
	if (State.Level >= GetMaxLevel(Weapon))
		return ELSWeaponChangeResult::MaxLevel;
	const ULSCastleWeaponData* Data = Definitions.FindChecked(Weapon);
	if (!Data->FindLevel(State.Level + 1))
		return ELSWeaponChangeResult::InvalidData;
	++State.Level;
	NotifyStateChanged(Weapon);
	return ELSWeaponChangeResult::Success;
}
float ULSCastleWeaponComponent::GetCooldownDuration(ELSCastleWeapon Weapon) const
{
	if (const float* Duration = CooldownDurations.Find(Weapon)) return *Duration;
	const FLSCastleWeaponSettings* Settings = FindSettings(Weapon);
	return Settings ? Settings->Cooldown : 0;
}

float ULSCastleWeaponComponent::GetRemainingCooldown(ELSCastleWeapon Weapon) const
{
	const FLSCastleWeaponState* State = States.Find(Weapon);
	return State && GetWorld() ? FMath::Max(0.0, State->ReadyTime - GetWorld()->GetTimeSeconds()) : 0;
}

ELSWeaponCastResult ULSCastleWeaponComponent::GetStatus(ELSCastleWeapon Weapon) const
{
	const FLSCastleWeaponSettings* Settings = FindSettings(Weapon);
	if (bBattleActionsEnded || LSBattleOutcome::AreActionsStopped(GetWorld()) ||
	    !HasBegunPlay() || !IsActive() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(GetOwner()) ||
	    GetOwner()->IsActorBeingDestroyed() || !ULSCombatTargetLibrary::IsCombatAlive(GetOwner()))
	{
		return ELSWeaponCastResult::Unavailable;
	}
	if (bCasting || bChangingState)
		return ELSWeaponCastResult::Busy;
	const FLSCastleWeaponState* State = States.Find(Weapon);
	if (!State || !Settings)
		return ELSWeaponCastResult::InvalidData;
	if (!State->bUnlocked)
		return ELSWeaponCastResult::Locked;
	if (!State->bEnabled)
		return ELSWeaponCastResult::Disabled;
	return GetRemainingCooldown(Weapon) > 0 ? ELSWeaponCastResult::Cooldown : ELSWeaponCastResult::Ready;
}

ELSWeaponCastResult ULSCastleWeaponComponent::ValidateAim(ELSCastleWeapon Weapon, const FVector& Aim) const
{
	const ELSWeaponCastResult Status = GetStatus(Weapon);
	if (Status != ELSWeaponCastResult::Ready)
		return Status;
	if (Aim.ContainsNaN())
		return ELSWeaponCastResult::InvalidAim;
	const double Distance = FVector::Dist2D(GetOwner()->GetActorLocation(), Aim);
	if (Weapon == ELSCastleWeapon::Cannon)
	{
		return Distance <= FindSettings(Weapon)->Range ? ELSWeaponCastResult::Ready : ELSWeaponCastResult::OutOfRange;
	}
	return Distance > KINDA_SMALL_NUMBER ? ELSWeaponCastResult::Ready : ELSWeaponCastResult::InvalidAim;
}

bool ULSCastleWeaponComponent::GetObstacleAimPoint(ALSPathObstacle* Target, FVector& OutAim) const
{
	if (!IsValid(Target) || Target->IsActorBeingDestroyed() || !Target->IsObstacleActive() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Target) || !GetWorld())
		return false;
	const UPrimitiveComponent* Body = Target->GetObstacleBody();
	if (!IsValid(Body) || !Body->IsQueryCollisionEnabled())
		return false;
	const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	if (Vision && Vision->IsVisionEnabled() && !Vision->IsPointVisible(Target->GetInteractionLocation()))
		return false;
	OutAim = Body->Bounds.Origin;
	return !OutAim.ContainsNaN();
}

ELSWeaponCastResult ULSCastleWeaponComponent::ValidateObstacleAim(ELSCastleWeapon Weapon,
                                                                 ALSPathObstacle* Target) const
{
	FVector Aim;
	return GetObstacleAimPoint(Target, Aim) ? ValidateAim(Weapon, Aim) : ELSWeaponCastResult::InvalidAim;
}

ELSWeaponCastResult ULSCastleWeaponComponent::CastWeaponAtObstacle(ELSCastleWeapon Weapon, ALSPathObstacle* Target)
{
	FVector Aim;
	if (!GetObstacleAimPoint(Target, Aim))
		return ELSWeaponCastResult::InvalidAim;
	return CastWeaponInternal(Weapon, Aim, true);
}

ELSWeaponCastResult ULSCastleWeaponComponent::CastWeapon(ELSCastleWeapon Weapon, const FVector& Aim)
{
	return CastWeaponInternal(Weapon, Aim, false);
}

ELSWeaponCastResult ULSCastleWeaponComponent::CastWeaponInternal(ELSCastleWeapon Weapon, const FVector& Aim,
                                                                bool bFirstBlocking)
{
	const ELSWeaponCastResult Status = ValidateAim(Weapon, Aim);
	if (Status != ELSWeaponCastResult::Ready)
		return Status;
	TGuardValue<bool> Guard(bCasting, true);
	const FLSCastleWeaponSettings Settings = *FindSettings(Weapon);
	if (Weapon == ELSCastleWeapon::Ballista)
	{
		const FVector Offset = Aim - GetOwner()->GetActorLocation();
		const FVector Direction = bFirstBlocking ? Offset.GetSafeNormal() : Offset.GetSafeNormal2D();
		const FTransform Transform(Direction.Rotation(), GetOwner()->GetActorLocation());
		ALSBasicProjectile* Projectile = GetWorld()->SpawnActorDeferred<ALSBasicProjectile>(
		    ALSBasicProjectile::StaticClass(), Transform, nullptr, nullptr,
		    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Projectile))
			return ELSWeaponCastResult::Unavailable;
		const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, GetOwner(), nullptr);
		const bool bLaunched = bFirstBlocking
		    ? Projectile->LaunchFirstBlocking(Request, Direction, Settings.ProjectileSpeed, Settings.Radius,
		                                      Settings.Range / Settings.ProjectileSpeed)
		    : Projectile->LaunchFirstEnemy(Request, Direction, Settings.ProjectileSpeed, Settings.Radius,
		                                   Settings.Range / Settings.ProjectileSpeed);
		if (!bLaunched)
		{
			Projectile->Destroy();
			return ELSWeaponCastResult::Unavailable;
		}
		Projectile->FinishSpawning(Transform);
		if (!IsValid(Projectile) || Projectile->IsActorBeingDestroyed())
			return ELSWeaponCastResult::Unavailable;
	}
	else
	{
		const FVector Start = GetOwner()->GetActorLocation() +
		                      (Weapon == ELSCastleWeapon::Cannon ? FVector(0, 0, 150) : FVector::ZeroVector);
		const FTransform Transform(FRotator::ZeroRotator, Start);
		ALSCastleProjectile* Projectile = GetWorld()->SpawnActorDeferred<ALSCastleProjectile>(
		    ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr,
		    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Projectile))
			return ELSWeaponCastResult::Unavailable;
		const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(Settings.Damage, GetOwner(), nullptr);
		if (!Projectile->Launch(Weapon, Settings, Request, Aim, bFirstBlocking))
		{
			Projectile->Destroy();
			return ELSWeaponCastResult::Unavailable;
		}
		Projectile->FinishSpawning(Transform);
		if (!IsValid(Projectile) || Projectile->IsActorBeingDestroyed())
			return ELSWeaponCastResult::Unavailable;
	}
	// 피해 알림에서 재진입하더라도 중복 시전하지 않게 먼저 쿨타임을 확정한다.
	States.FindChecked(Weapon).ReadyTime = GetWorld()->GetTimeSeconds() + Settings.Cooldown;
	CooldownDurations.Add(Weapon, Settings.Cooldown);
	LastCastSettings = Settings;
	LastWeapon = Weapon;
	LastAim = Aim;
	LastCastTime = GetWorld()->GetTimeSeconds();
	return ELSWeaponCastResult::Ready;
}

bool ULSCastleWeaponComponent::GetRecentCast(ELSCastleWeapon& Weapon, FVector& Aim,
                                             FLSCastleWeaponSettings* Settings) const
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() - LastCastTime > 0.35)
		return false;
	Weapon = LastWeapon;
	Aim = LastAim;
	if (Settings)
		*Settings = LastCastSettings;
	return true;
}
