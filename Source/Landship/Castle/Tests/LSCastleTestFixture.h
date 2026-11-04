#pragma once

#include "Castle/LSCastle.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace LSCastleTestFixture
{
inline ALSCastle* Spawn(UWorld* World, const FVector& Location = FVector::ZeroVector,
    const FRotator& Rotation = FRotator::ZeroRotator, const FActorSpawnParameters& Parameters = FActorSpawnParameters())
{
    const FTransform Transform(Rotation, Location);
    ALSCastle* Castle = World->SpawnActorDeferred<ALSCastle>(ALSCastle::StaticClass(), Transform,
        Parameters.Owner, Parameters.Instigator, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Castle) return nullptr;
    FStructProperty* Property = FindFProperty<FStructProperty>(ALSCastle::StaticClass(), TEXT("StatsRow"));
    FDataTableRowHandle* Row = Property->ContainerPtrToValuePtr<FDataTableRowHandle>(Castle);
    Row->DataTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Landship/Combat/Data/DT_CastleStats.DT_CastleStats"));
    Row->RowName = TEXT("BattlefieldPrototype");
    Castle->FinishSpawning(Transform);
    return Castle;
}
}
