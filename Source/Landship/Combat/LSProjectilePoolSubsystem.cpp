#include "Combat/LSProjectilePoolSubsystem.h"
#include "Combat/LSBasicProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

namespace
{
// 양측 각 100명의 기본 궁병과 기본 비행 수명 동안의 동시 화살에 여유를 둔 시작값이다. 실측에 따라 조정한다.
TAutoConsoleVariable<int32> LSProjectileMaxInactive(
    TEXT("ls.Pool.MaxInactivePerClass"), 640,
    TEXT("Retained inactive arrows per class. 0 is unlimited. Active arrows are never evicted."));
int32 GetLSProjectileRetentionLimit()
{
	return FMath::Max(0, LSProjectileMaxInactive.GetValueOnGameThread());
}
#if !UE_BUILD_SHIPPING
FAutoConsoleCommandWithWorld LSProjectilePoolSnapshot(TEXT("ls.Pool.Snapshot"),
                                                      TEXT("Log arrow pool counters for this world."),
                                                      FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
	                                                      if (World)
		                                                      if (ULSProjectilePoolSubsystem* Pool =
		                                                              World->GetSubsystem<ULSProjectilePoolSubsystem>())
			                                                      Pool->WriteDiagnostics();
                                                      }));
FAutoConsoleCommandWithWorld LSProjectilePoolResetCounters(
    TEXT("ls.Pool.ResetCounters"), TEXT("Reset arrow pool counters without changing objects or leases."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
	    if (World)
		    if (ULSProjectilePoolSubsystem* Pool = World->GetSubsystem<ULSProjectilePoolSubsystem>())
			    Pool->ResetDiagnostics();
    }));
#endif
} // namespace

bool ULSProjectilePoolSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool ULSProjectilePoolSubsystem::CanUseClass(TSubclassOf<ALSBasicProjectile> ProjectileClass) const
{
	return !bShuttingDown && !bReturningAll && GetWorld() && !GetWorld()->bIsTearingDown && ProjectileClass &&
	       !ProjectileClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
	       ProjectileClass->GetDefaultObject<ALSBasicProjectile>()->SupportsPooling();
}

ALSBasicProjectile* ULSProjectilePoolSubsystem::SpawnInactive(TSubclassOf<ALSBasicProjectile> ProjectileClass)
{
	if (!CanUseClass(ProjectileClass))
		return nullptr;
	ALSBasicProjectile* Projectile = GetWorld()->SpawnActorDeferred<ALSBasicProjectile>(
	    ProjectileClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Projectile))
		return nullptr;
	if (!Projectile->PrepareForPool(this))
	{
		Projectile->Destroy();
		return nullptr;
	}
	Buckets.FindOrAdd(ProjectileClass).Owned.Add(Projectile);
	Projectile->OnDestroyed.AddUniqueDynamic(this, &ULSProjectilePoolSubsystem::OnProjectileDestroyed);
#if !UE_BUILD_SHIPPING
	++Counters.FindOrAdd(ProjectileClass.Get()).Created;
#endif
	Projectile->FinishSpawning(FTransform::Identity);
	if (!IsValid(Projectile) || Projectile->IsActorBeingDestroyed() || !CanUseClass(ProjectileClass))
	{
		Retire(Projectile);
		return nullptr;
	}
	return Projectile;
}

int32 ULSProjectilePoolSubsystem::PrewarmPool(TSubclassOf<ALSBasicProjectile> ProjectileClass,
                                              int32 DesiredInactiveCount)
{
	if (!CanUseClass(ProjectileClass) || DesiredInactiveCount < 0)
		return 0;
	const int32 Limit = GetLSProjectileRetentionLimit();
	const int32 Target = Limit > 0 ? FMath::Min(DesiredInactiveCount, Limit) : DesiredInactiveCount;
	// 액터의 생성 콜백은 다른 클래스를 요청할 수 있다. TMap의 값 참조를 콜백 너머에 보관하지 않는다.
	while (Buckets.FindOrAdd(ProjectileClass).Inactive.Num() < Target)
	{
		ALSBasicProjectile* Projectile = SpawnInactive(ProjectileClass);
		if (!Projectile)
			break;
		Buckets.FindOrAdd(ProjectileClass).Inactive.Add(Projectile);
	}
	return Buckets.FindOrAdd(ProjectileClass).Inactive.Num();
}

FLSProjectileLease ULSProjectilePoolSubsystem::AcquireProjectile(TSubclassOf<ALSBasicProjectile> ProjectileClass,
                                                                 const FTransform& Transform, AActor* Owner,
                                                                 APawn* Instigator)
{
	if (!CanUseClass(ProjectileClass) || Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized() ||
	    Transform.GetScale3D().GetMin() <= 0 ||
	    (Owner && (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->GetWorld() != GetWorld())) ||
	    (Instigator &&
	     (!IsValid(Instigator) || Instigator->IsActorBeingDestroyed() || Instigator->GetWorld() != GetWorld())))
		return {};
	ALSBasicProjectile* Projectile = LSPoolStorage::PopValid(Buckets.FindOrAdd(ProjectileClass).Inactive);
	const bool bReused = Projectile != nullptr;
	if (!Projectile)
		Projectile = SpawnInactive(ProjectileClass);
	if (!Projectile)
		return {};
	const uint64 Serial = Leases.Acquire(Projectile);
	if (!Serial)
	{
		Retire(Projectile);
		return {};
	}
#if !UE_BUILD_SHIPPING
	FLSProjectilePoolCounters& Stats = Counters.FindOrAdd(ProjectileClass.Get());
	++Stats.Acquired;
	Stats.Reused += bReused ? 1 : 0;
	Stats.PeakActive = FMath::Max(Stats.PeakActive, ++Stats.Active);
#endif
	const FLSProjectileLease Lease{Projectile, Serial};
	if (!Projectile->ActivateFromPool(this, Serial, Transform, Owner, Instigator) || !IsAcquired(Projectile, Serial))
	{
		ReleaseProjectile(Lease);
		return {};
	}
	return Lease;
}

bool ULSProjectilePoolSubsystem::IsAcquired(const ALSBasicProjectile* Projectile, uint64 Serial) const
{
	return IsValid(Projectile) && !Projectile->IsActorBeingDestroyed() && Projectile->GetWorld() == GetWorld() &&
	       Leases.Contains(Projectile, Serial);
}

bool ULSProjectilePoolSubsystem::ReleaseProjectile(const FLSProjectileLease& Lease)
{
	ALSBasicProjectile* Projectile = Lease.Projectile.Get();
	if (!IsAcquired(Projectile, Lease.Serial) || !Leases.Release(Projectile, Lease.Serial))
		return false;
#if !UE_BUILD_SHIPPING
	FLSProjectilePoolCounters& Stats = Counters.FindOrAdd(Projectile->GetClass());
	--Stats.Active;
	++Stats.Released;
#endif
	// 대여를 먼저 소비한다. 비활성화 중 재진입 반환은 위 검사에서 거부된다.
	if (!Projectile->DeactivateForPool(this, Lease.Serial) || !IsValid(Projectile) ||
	    Projectile->IsActorBeingDestroyed())
	{
		Retire(Projectile);
		return true;
	}
	FLSProjectilePoolBucket& Bucket = Buckets.FindOrAdd(Projectile->GetClass());
	if (!bShuttingDown && !GetWorld()->bIsTearingDown &&
	    LSPoolStorage::CanRetain(Bucket.Inactive.Num(), GetLSProjectileRetentionLimit()))
		Bucket.Inactive.Add(Projectile);
	else
		Retire(Projectile);
	return true;
}

void ULSProjectilePoolSubsystem::Retire(ALSBasicProjectile* Projectile)
{
	if (!Projectile)
		return;
	OnProjectileDestroyed(Projectile);
	Projectile->OnDestroyed.RemoveDynamic(this, &ULSProjectilePoolSubsystem::OnProjectileDestroyed);
	if (IsValid(Projectile) && !Projectile->IsActorBeingDestroyed())
		Projectile->Destroy();
}

void ULSProjectilePoolSubsystem::OnProjectileDestroyed(AActor* Actor)
{
	ALSBasicProjectile* Projectile = Cast<ALSBasicProjectile>(Actor);
	FLSProjectilePoolBucket* Bucket = Projectile ? Buckets.Find(Projectile->GetClass()) : nullptr;
	if (!Bucket || Bucket->Owned.Remove(Projectile) == 0)
		return;
	Bucket->Inactive.RemoveSingleSwap(Projectile, EAllowShrinking::No);
#if !UE_BUILD_SHIPPING
	FLSProjectilePoolCounters& Stats = Counters.FindOrAdd(Projectile->GetClass());
	if (Leases.HasObject(Projectile))
		--Stats.Active;
	++Stats.Retired;
#endif
	Leases.Forget(Projectile);
}

void ULSProjectilePoolSubsystem::ReleaseAll()
{
	if (bReturningAll || bShuttingDown)
		return;
	TGuardValue<bool> Guard(bReturningAll, true);
	TArray<FLSProjectileLease> Outstanding;
	for (const auto& Pair : Buckets)
		for (ALSBasicProjectile* Projectile : Pair.Value.Owned)
			if (IsValid(Projectile) && IsAcquired(Projectile, Projectile->GetActivationSerial()))
				Outstanding.Add({Projectile, Projectile->GetActivationSerial()});
	for (const FLSProjectileLease& Lease : Outstanding)
		ReleaseProjectile(Lease);
}

void ULSProjectilePoolSubsystem::Deinitialize()
{
	bShuttingDown = true;
	Leases.Reset();
	TArray<ALSBasicProjectile*> Owned;
	for (const auto& Pair : Buckets)
		for (ALSBasicProjectile* Projectile : Pair.Value.Owned)
			Owned.Add(Projectile);
	for (ALSBasicProjectile* Projectile : Owned)
		Retire(Projectile);
	Buckets.Empty();
#if !UE_BUILD_SHIPPING
	Counters.Empty();
#endif
	Super::Deinitialize();
}

#if !UE_BUILD_SHIPPING
void ULSProjectilePoolSubsystem::WriteDiagnostics() const
{
	if (Counters.IsEmpty())
		UE_LOG(LogTemp, Display, TEXT("ArrowPool World=%s No tracked projectile classes"), *GetNameSafe(GetWorld()));
	for (const auto& Pair : Counters)
	{
		const UClass* Class = Pair.Key.Get();
		const FLSProjectilePoolCounters& Stats = Pair.Value;
		const FLSProjectilePoolBucket* Bucket = Class ? Buckets.Find(const_cast<UClass*>(Class)) : nullptr;
		UE_LOG(
		    LogTemp, Display,
		    TEXT(
		        "ArrowPool World=%s Class=%s Active=%d Inactive=%d Peak=%d Created=%lld Reused=%lld Acquired=%lld Released=%lld Retired=%lld"),
		    *GetNameSafe(GetWorld()), *GetNameSafe(Class), Stats.Active, Bucket ? Bucket->Inactive.Num() : 0,
		    Stats.PeakActive, Stats.Created, Stats.Reused, Stats.Acquired, Stats.Released, Stats.Retired);
	}
}

void ULSProjectilePoolSubsystem::ResetDiagnostics()
{
	for (auto& Pair : Counters)
	{
		const int32 Active = Pair.Value.Active;
		Pair.Value = {};
		Pair.Value.Active = Active;
		Pair.Value.PeakActive = Active;
	}
}
#endif
