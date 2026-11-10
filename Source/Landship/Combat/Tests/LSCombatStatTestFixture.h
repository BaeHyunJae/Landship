#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSCombatStatRows.h"
#include "Troops/LSUnit.h"
#include "Troops/LSTroopUnit.h"
#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"

namespace LSCombatStatTestFixture
{
inline FLSUnitStatRow UnitStats(float Health = 180.0f)
{
	FLSUnitStatRow Row;
	Row.Health = Health;
	Row.AttackPower = 12.0f;
	Row.AttackInterval = 1.0f;
	Row.AttackRange = 150.0f;
	Row.DetectionRange = 1000.0f;
	Row.VisionRadius = 1400.0f;
	Row.MovementSpeed = 300.0f;
	Row.Armor = 0.0f;
	Row.ArmorPenetration = 0.0f;
	return Row;
}

/** 최종 병종 지정 뒤, BeginPlay 전에 호출한다. 임시 테이블은 StatsRow의 강한 참조로 유지한다. */
inline bool ConfigureUnit(ALSUnit* Unit, const FLSUnitStatRow& Stats)
{
	if (!IsValid(Unit) || Unit->HasActorBegunPlay()) return false;
	FStructProperty* StatsProperty = FindFProperty<FStructProperty>(ALSUnit::StaticClass(), TEXT("StatsRow"));
	if (!StatsProperty) return false;
	FName RowName = TEXT("FixedStats");
	ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit);
	FSoftObjectProperty* TableProperty = nullptr;
	if (Troop)
	{
		RowName = ALSTroopUnit::GetStatsRowName(Troop->GetTroopType());
		TableProperty = FindFProperty<FSoftObjectProperty>(ALSTroopUnit::StaticClass(), TEXT("TroopStatsTable"));
		if (RowName.IsNone() || !TableProperty) return false;
	}
	UDataTable* Table = NewObject<UDataTable>(Unit);
	Table->RowStruct = FLSUnitStatRow::StaticStruct();
	Table->AddRow(RowName, Stats);
	FDataTableRowHandle* Handle = StatsProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Unit);
	Handle->DataTable = Table;
	Handle->RowName = RowName;
	if (Troop)
	{
		// 병력은 BeginPlay에서 이 테이블로 StatsRow를 다시 선택한다.
		*TableProperty->ContainerPtrToValuePtr<TSoftObjectPtr<UDataTable>>(Troop) = TSoftObjectPtr<UDataTable>(Table);
	}
	return true;
}
}

#endif
