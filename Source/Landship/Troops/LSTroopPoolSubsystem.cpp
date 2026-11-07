#include "Troops/LSTroopPoolSubsystem.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "AIController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
// 양측 최대 편성을 같은 병종으로 구성해 복원하는 작업량을 시작 기준으로 삼는다.
TAutoConsoleVariable<int32> LSTroopMaxInactive(
    TEXT("ls.TroopPool.MaxInactivePerBucket"), 200,
    TEXT("Retained inactive troops per class/type. 0 is unlimited; active troops are not limited."));
int32 GetLSTroopRetentionLimit()
{
	return FMath::Max(0, LSTroopMaxInactive.GetValueOnGameThread());
}
#if !UE_BUILD_SHIPPING
FAutoConsoleCommandWithWorld LSTroopPoolSnapshot(TEXT("ls.TroopPool.Snapshot"), TEXT("Log troop pool counters."),
                                                 FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) {
	                                                 if (World)
		                                                 if (ULSTroopPoolSubsystem* Pool =
		                                                         World->GetSubsystem<ULSTroopPoolSubsystem>())
			                                                 Pool->WriteDiagnostics();
                                                 }));
#endif
} // namespace

bool ULSTroopPoolSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool ULSTroopPoolSubsystem::CanUse(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type) const
{
	return !bShuttingDown && GetWorld() && !GetWorld()->bIsTearingDown && UnitClass == ALSTroopUnit::StaticClass() &&
	       !ALSTroopUnit::GetStatsRowName(Type).IsNone();
}

ALSTroopUnit* ULSTroopPoolSubsystem::SpawnInactive(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type)
{
	if (!CanUse(UnitClass, Type))
		return nullptr;
	ALSTroopUnit* Unit = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(UnitClass, FTransform::Identity, nullptr, nullptr,
	                                                                  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Unit)
		return nullptr;
	Unit->SetInitialTroopType(Type);
	Unit->SetInitialFaction(ELSFaction::Ally);
	Buckets.FindOrAdd(UnitClass).Types.FindOrAdd(Type).Owned.Add(Unit);
	Unit->OnDestroyed.AddUniqueDynamic(this, &ULSTroopPoolSubsystem::OnUnitDestroyed);
#if !UE_BUILD_SHIPPING
	++Created;
#endif
	Unit->FinishSpawning(FTransform::Identity);
	if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || !CanUse(UnitClass, Type) ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Unit) || !Unit->PrepareForPool(this))
	{
		Retire(Unit);
		return nullptr;
	}
	return Unit;
}

FLSTroopLease ULSTroopPoolSubsystem::AcquireUnit(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type,
                                                 ELSFaction Faction, const FTransform& Transform, AActor* Owner)
{
	if (!CanUse(UnitClass, Type) || !LSFaction::IsValid(Faction) || Transform.ContainsNaN() ||
	    !Transform.GetRotation().IsNormalized() || Transform.GetScale3D().GetMin() <= 0 ||
	    (Owner && (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->GetWorld() != GetWorld())))
		return {};
	ALSTroopUnit* Unit = LSPoolStorage::PopValid(Buckets.FindOrAdd(UnitClass).Types.FindOrAdd(Type).Inactive);
	const bool bReused = Unit != nullptr;
	if (!Unit)
		Unit = SpawnInactive(UnitClass, Type);
	if (!Unit)
		return {};
	const uint64 Serial = Leases.Acquire(Unit);
	if (!Serial)
	{
		Retire(Unit);
		return {};
	}
#if !UE_BUILD_SHIPPING
	++Acquired;
	Reused += bReused ? 1 : 0;
	PeakActive = FMath::Max(PeakActive, ++Active);
#endif
	const FLSTroopLease Lease{Unit, Serial};
	if (!Unit->ActivateFromPool(this, Serial, Faction, Transform, Owner) || !IsAcquired(Unit, Serial))
	{
		if (!ReleaseUnit(Lease))
			Retire(Unit);
		return {};
	}
	return Lease;
}

bool ULSTroopPoolSubsystem::IsAcquired(const ALSTroopUnit* Unit, uint64 Serial) const
{
	return IsValid(Unit) && !Unit->IsActorBeingDestroyed() && Unit->GetWorld() == GetWorld() &&
	       Leases.Contains(Unit, Serial);
}

bool ULSTroopPoolSubsystem::ReleaseUnit(const FLSTroopLease& Lease)
{
	ALSTroopUnit* Unit = Lease.Unit.Get();
	if (!IsAcquired(Unit, Lease.Serial) || !Unit->CanReturnToPool() || !Leases.Release(Unit, Lease.Serial))
		return false;
#if !UE_BUILD_SHIPPING
	--Active;
	++Released;
#endif
	if (!Unit->DeactivateForPool(this, Lease.Serial) || !IsValid(Unit) || Unit->IsActorBeingDestroyed())
	{
		Retire(Unit);
		return true;
	}
	FLSTroopPoolBucket& Bucket = Buckets.FindOrAdd(Unit->GetClass()).Types.FindOrAdd(Unit->GetTroopType());
	if (!bShuttingDown && !GetWorld()->bIsTearingDown &&
	    LSPoolStorage::CanRetain(Bucket.Inactive.Num(), GetLSTroopRetentionLimit()))
		Bucket.Inactive.Add(Unit);
	else
		Retire(Unit);
	return true;
}

int32 ULSTroopPoolSubsystem::PrewarmPool(TSubclassOf<ALSTroopUnit> UnitClass, ELSTroopType Type,
                                         int32 DesiredInactiveCount)
{
	if (!CanUse(UnitClass, Type) || DesiredInactiveCount < 0)
		return 0;
	const int32 Limit = GetLSTroopRetentionLimit();
	const int32 Target = Limit ? FMath::Min(DesiredInactiveCount, Limit) : DesiredInactiveCount;
	while (Buckets.FindOrAdd(UnitClass).Types.FindOrAdd(Type).Inactive.Num() < Target)
	{
		ALSTroopUnit* Unit = SpawnInactive(UnitClass, Type);
		if (!Unit)
			break;
		Buckets.FindOrAdd(UnitClass).Types.FindOrAdd(Type).Inactive.Add(Unit);
	}
	return Buckets.FindOrAdd(UnitClass).Types.FindOrAdd(Type).Inactive.Num();
}

void ULSTroopPoolSubsystem::OnUnitDestroyed(AActor* Actor)
{
	ALSTroopUnit* Unit = Cast<ALSTroopUnit>(Actor);
	FLSTroopPoolClassBuckets* ClassBuckets = Unit ? Buckets.Find(Unit->GetClass()) : nullptr;
	FLSTroopPoolBucket* Bucket = ClassBuckets ? ClassBuckets->Types.Find(Unit->GetTroopType()) : nullptr;
	if (!Bucket || Bucket->Owned.Remove(Unit) == 0)
		return;
	Bucket->Inactive.RemoveSingleSwap(Unit, EAllowShrinking::No);
#if !UE_BUILD_SHIPPING
	if (Leases.HasObject(Unit))
		--Active;
	++Retired;
#endif
	Leases.Forget(Unit);
	if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
		AI->Destroy();
}

void ULSTroopPoolSubsystem::Retire(ALSTroopUnit* Unit)
{
	if (!Unit)
		return;
	OnUnitDestroyed(Unit);
	Unit->OnDestroyed.RemoveDynamic(this, &ULSTroopPoolSubsystem::OnUnitDestroyed);
	if (IsValid(Unit) && !Unit->IsActorBeingDestroyed())
		Unit->Destroy();
}

void ULSTroopPoolSubsystem::Deinitialize()
{
	bShuttingDown = true;
	Leases.Reset();
	TArray<ALSTroopUnit*> Owned;
	for (const auto& Class : Buckets)
		for (const auto& Type : Class.Value.Types)
			for (ALSTroopUnit* Unit : Type.Value.Owned)
				Owned.Add(Unit);
	for (ALSTroopUnit* Unit : Owned)
		Retire(Unit);
	Buckets.Empty();
	Super::Deinitialize();
}

#if !UE_BUILD_SHIPPING
void ULSTroopPoolSubsystem::WriteDiagnostics() const
{
	int32 Inactive = 0;
	for (const auto& Class : Buckets)
		for (const auto& Type : Class.Value.Types)
		{
			Inactive += Type.Value.Inactive.Num();
			UE_LOG(LogTemp, Display, TEXT("TroopPool Class=%s Type=%s Inactive=%d"), *GetNameSafe(Class.Key.Get()),
			       *ALSTroopUnit::GetStatsRowName(Type.Key).ToString(), Type.Value.Inactive.Num());
		}
	UE_LOG(
	    LogTemp, Display,
	    TEXT(
	        "TroopPool World=%s Active=%d Inactive=%d Peak=%d Created=%lld Reused=%lld Acquired=%lld Released=%lld Retired=%lld"),
	    *GetNameSafe(GetWorld()), Active, Inactive, PeakActive, Created, Reused, Acquired, Released, Retired);
}
#endif
