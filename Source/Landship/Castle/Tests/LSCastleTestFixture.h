#pragma once

#include "Castle/LSCastle.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace LSCastleTestFixture
{
inline ALSCastle* SpawnWithStats(UWorld* World, const FLSCastleStatRow& Stats,
    const FVector& Location = FVector::ZeroVector, const FRotator& Rotation = FRotator::ZeroRotator)
{
    const FTransform Transform(Rotation, Location);
    ALSCastle* Castle = World->SpawnActorDeferred<ALSCastle>(ALSCastle::StaticClass(), Transform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Castle) return nullptr;
    FStructProperty* Property = FindFProperty<FStructProperty>(ALSCastle::StaticClass(), TEXT("StatsRow"));
    if (!Property)
    {
        Castle->Destroy();
        return nullptr;
    }
    UDataTable* Table = NewObject<UDataTable>(Castle);
    Table->RowStruct = FLSCastleStatRow::StaticStruct();
    Table->AddRow(TEXT("FixedStats"), Stats);
    FDataTableRowHandle* Row = Property->ContainerPtrToValuePtr<FDataTableRowHandle>(Castle);
    Row->DataTable = Table;
    Row->RowName = TEXT("FixedStats");
    Castle->FinishSpawning(Transform);
    return Castle;
}

inline FLSCastleStatRow FixedStats()
{
    FLSCastleStatRow Row;
    Row.Health = 1000.0f;
    Row.VisionRadius = 1600.0f;
    Row.MovementSpeed = 400.0f;
    return Row;
}

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
