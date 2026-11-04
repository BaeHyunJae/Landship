#pragma once

#include "Match/LSAutoBattleScenario.h"

/** 전투 중 액터 상태가 아닌 준비 편성만 저장한다. ID는 로드 때 새로 부여한다. */
struct LANDSHIP_API FLSAutoBattlePreset
{
	int32 Version = 1;
	int32 Budget = 1000;
	int32 PopulationLimit = 100;
	TArray<int32> UnitCosts = {40, 60, 90};
	FVector AllyCastle = FVector::ZeroVector;
	FVector EnemyCastle = FVector::ZeroVector;
	ELSCastleWeapon AllyWeapon = ELSCastleWeapon::Cannon;
	ELSCastleWeapon EnemyWeapon = ELSCastleWeapon::Cannon;
	TArray<FLSAutoBattlePlacement> Allies;
	TArray<FLSAutoBattlePlacement> Enemies;
	bool IsValid() const;
};

/** 이름은 파일 경로가 아니다. 지정 폴더 안에서만 UTF-8 JSON을 원자적으로 교체한다. */
class LANDSHIP_API FLSAutoBattlePresetStore
{
  public:
	static FString Directory();
	static bool IsValidName(const FString& Name);
	static TArray<FString> List();
	static bool Save(const FString& Name, const FLSAutoBattlePreset& Preset, bool bOverwrite, FString& Error);
	static bool Load(const FString& Name, FLSAutoBattlePreset& OutPreset, FString& Error);
	static bool Encode(const FLSAutoBattlePreset& Preset, FString& OutJson);
	static bool Decode(const FString& Json, FLSAutoBattlePreset& OutPreset);
};
