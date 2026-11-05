#include "Match/LSAutoBattlePreset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace
{
const TCHAR* AutoBattlePresetTroopNames[] = {
    TEXT("Spearman"), TEXT("Archer"), TEXT("Cavalry"), TEXT("Medic"), TEXT("Shieldbearer"), TEXT("Mortar"), TEXT("Bomber")};
static_assert(UE_ARRAY_COUNT(AutoBattlePresetTroopNames) == ALSTroopUnit::GetTroopTypeCount());
TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V)
{
	return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
}
bool ReadVector(const TArray<TSharedPtr<FJsonValue>>& Values, FVector& V)
{
	double X, Y, Z;
	if (Values.Num() != 3 || !Values[0].IsValid() || !Values[1].IsValid() || !Values[2].IsValid() ||
	    !Values[0]->TryGetNumber(X) || !Values[1]->TryGetNumber(Y) || !Values[2]->TryGetNumber(Z))
	{
		return false;
	}
	V = FVector(X, Y, Z);
	return !V.ContainsNaN();
}
bool ReadInt(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, int32& Out)
{
	double N;
	if (!Json->TryGetNumberField(Key, N) || !FMath::IsFinite(N) || N < MIN_int32 || N > MAX_int32 ||
	    N != FMath::TruncToDouble(N))
	{
		return false;
	}
	Out = static_cast<int32>(N);
	return true;
}
// FileManager::Move는 교체 전에 원본을 지우므로, 실패 시 기존 프리셋을 잃지 않는 플랫폼 교체를 사용한다.
bool CommitPresetFile(const FString& Temp, const FString& Path, bool bOverwrite)
{
#if PLATFORM_WINDOWS
	return !!::MoveFileExW(*Temp, *Path, MOVEFILE_WRITE_THROUGH | (bOverwrite ? MOVEFILE_REPLACE_EXISTING : 0));
#else
	return bOverwrite ? std::rename(TCHAR_TO_UTF8(*Temp), TCHAR_TO_UTF8(*Path)) == 0
	                  : IFileManager::Get().Move(*Path, *Temp, false, true, false, true);
#endif
}
FString Filename(const FString& Name)
{
	return FPaths::Combine(FLSAutoBattlePresetStore::Directory(), TEXT("Preset_") + Name + TEXT(".json"));
}
} // namespace

bool FLSAutoBattlePreset::IsValid() const
{
	if (Version != 4 ||
	    UnitCosts.Num() != ALSTroopUnit::GetTroopTypeCount() ||
	    AllyCastle.ContainsNaN() || EnemyCastle.ContainsNaN() || static_cast<uint8>(AllyWeapon) > 2 ||
	    static_cast<uint8>(EnemyWeapon) > 2 || Enemies.Num() > 100)
	{
		return false;
	}
	for (int32 Cost : UnitCosts)
	{
		if (Cost <= 0)
		{
			return false;
		}
	}
	for (const auto& P : Allies)
	{
		if (ALSTroopUnit::GetStatsRowName(P.Type).IsNone() || P.Position.ContainsNaN())
		{
			return false;
		}
	}
	for (const auto& P : Enemies)
	{
		if (ALSTroopUnit::GetStatsRowName(P.Type).IsNone() || P.Position.ContainsNaN())
		{
			return false;
		}
	}
	return true;
}

FString FLSAutoBattlePresetStore::Directory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoBattlePresets")));
}
bool FLSAutoBattlePresetStore::IsValidName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 64 || Name != Name.TrimStartAndEnd())
	{
		return false;
	}
	for (TCHAR C : Name)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-') && C != TEXT(' '))
		{
			return false;
		}
	}
	return true;
}
TArray<FString> FLSAutoBattlePresetStore::List()
{
	TArray<FString> Files, Result;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory(), TEXT("Preset_*.json")), true, false);
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File).RightChop(7);
		if (IsValidName(Name))
		{
			Result.Add(Name);
		}
	}
	Result.Sort();
	return Result;
}
bool FLSAutoBattlePresetStore::Encode(const FLSAutoBattlePreset& P, FString& OutJson)
{
	if (!P.IsValid())
	{
		return false;
	}
	auto J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), P.Version);
	J->SetNumberField(TEXT("budget"), P.Budget);
	J->SetNumberField(TEXT("populationLimit"), P.PopulationLimit);
	J->SetArrayField(TEXT("allyCastle"), VectorJson(P.AllyCastle));
	J->SetArrayField(TEXT("enemyCastle"), VectorJson(P.EnemyCastle));
	J->SetNumberField(TEXT("allyWeapon"), static_cast<uint8>(P.AllyWeapon));
	J->SetNumberField(TEXT("enemyWeapon"), static_cast<uint8>(P.EnemyWeapon));
	TArray<TSharedPtr<FJsonValue>> Costs;
	for (int32 C : P.UnitCosts)
	{
		Costs.Add(MakeShared<FJsonValueNumber>(C));
	}
	J->SetArrayField(TEXT("unitCosts"), Costs);
	auto PutUnits = [&J](const TCHAR* Key, const TArray<FLSAutoBattlePlacement>& Units)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const auto& U : Units)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"), AutoBattlePresetTroopNames[static_cast<uint8>(U.Type)]);
			O->SetArrayField(TEXT("position"), VectorJson(U.Position));
			Values.Add(MakeShared<FJsonValueObject>(O));
		}
		J->SetArrayField(Key, Values);
	};
	PutUnits(TEXT("allies"), P.Allies);
	PutUnits(TEXT("enemies"), P.Enemies);
	FString Json;
	if (!FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Json)))
	{
		return false;
	}
	OutJson = MoveTemp(Json);
	return true;
}
bool FLSAutoBattlePresetStore::Decode(const FString& Json, FLSAutoBattlePreset& OutPreset)
{
	if (Json.Len() > 1024 * 1024)
	{
		return false;
	}
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), J) || !J.IsValid())
	{
		return false;
	}
	FLSAutoBattlePreset P;
	int32 AllyWeapon, EnemyWeapon;
	if (!ReadInt(J, TEXT("version"), P.Version) || !ReadInt(J, TEXT("budget"), P.Budget) ||
	    !ReadInt(J, TEXT("populationLimit"), P.PopulationLimit) || !ReadInt(J, TEXT("allyWeapon"), AllyWeapon) ||
	    AllyWeapon < 0 || AllyWeapon > 2 || !ReadInt(J, TEXT("enemyWeapon"), EnemyWeapon) || EnemyWeapon < 0 ||
	    EnemyWeapon > 2)
	{
		return false;
	}
	P.AllyWeapon = static_cast<ELSCastleWeapon>(AllyWeapon);
	P.EnemyWeapon = static_cast<ELSCastleWeapon>(EnemyWeapon);
	constexpr int32 SavedTroopCounts[] = {0, 3, 4, 6, ALSTroopUnit::GetTroopTypeCount()};
	const int32 SavedTroopCount = P.Version > 0 && P.Version < UE_ARRAY_COUNT(SavedTroopCounts)
	    ? SavedTroopCounts[P.Version] : 0;
	const TArray<TSharedPtr<FJsonValue>>* Values;
	if (!J->TryGetArrayField(TEXT("allyCastle"), Values) || !ReadVector(*Values, P.AllyCastle) ||
	    !J->TryGetArrayField(TEXT("enemyCastle"), Values) || !ReadVector(*Values, P.EnemyCastle) ||
	    !J->TryGetArrayField(TEXT("unitCosts"), Values) ||
	    SavedTroopCount == 0 || Values->Num() != SavedTroopCount)
	{
		return false;
	}
	P.UnitCosts.Reset();
	for (const auto& V : *Values)
	{
		double C;
		if (!V.IsValid() || !V->TryGetNumber(C) || !FMath::IsFinite(C) || C < 1 || C > MAX_int32 ||
		    C != FMath::TruncToDouble(C))
		{
			return false;
		}
		P.UnitCosts.Add(static_cast<int32>(C));
	}
	auto GetUnits = [&J, SavedTroopCount](const TCHAR* Key, TArray<FLSAutoBattlePlacement>& Units, bool bLimitEnemyCount)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (!J->TryGetArrayField(Key, Array) || (bLimitEnemyCount && Array->Num() > 100))
		{
			return false;
		}
		for (const auto& V : *Array)
		{
			const TSharedPtr<FJsonObject>* O;
			FString Type;
			const TArray<TSharedPtr<FJsonValue>>* Position;
			if (!V.IsValid() || !V->TryGetObject(O) || !O->IsValid() || !(*O)->TryGetStringField(TEXT("type"), Type) ||
			    !(*O)->TryGetArrayField(TEXT("position"), Position))
			{
				return false;
			}
			FLSAutoBattlePlacement U;
			int32 Index = INDEX_NONE;
			for (int32 I = 0; I < SavedTroopCount; ++I)
			{
				if (Type == AutoBattlePresetTroopNames[I])
				{
					Index = I;
				}
			}
			if (Index == INDEX_NONE || !ReadVector(*Position, U.Position))
			{
				return false;
			}
			U.Type = static_cast<ELSTroopType>(Index);
			Units.Add(U);
		}
		return true;
	};
	if (!GetUnits(TEXT("allies"), P.Allies, false) || !GetUnits(TEXT("enemies"), P.Enemies, true))
	{
		return false;
	}
	for (int32 Index = SavedTroopCount; Index < ALSTroopUnit::GetTroopTypeCount(); ++Index)
	{
		P.UnitCosts.Add(ALSAutoBattleScenario::GetDefaultUnitCost(static_cast<ELSTroopType>(Index)));
	}
	P.Version = 4;
	if (!P.IsValid())
		return false;
	OutPreset = MoveTemp(P);
	return true;
}
bool FLSAutoBattlePresetStore::Save(const FString& Name, const FLSAutoBattlePreset& Preset, bool bOverwrite,
                                    FString& Error)
{
	FString Json;
	if (!IsValidName(Name) || !Encode(Preset, Json))
	{
		Error = TEXT("이름 또는 편성 데이터가 잘못되었습니다.");
		return false;
	}
	const FString Path = Filename(Name);
	if (!bOverwrite && IFileManager::Get().FileExists(*Path))
	{
		Error = TEXT("같은 이름이 있습니다. 다른 이름 또는 덮어쓰기를 선택하세요.");
		return false;
	}
	const FString Temp = Path + TEXT(".") + FGuid::NewGuid().ToString() + TEXT(".tmp");
	if (!IFileManager::Get().MakeDirectory(*Directory(), true) ||
	    !FFileHelper::SaveStringToFile(Json, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
	    !CommitPresetFile(Temp, Path, bOverwrite))
	{
		IFileManager::Get().Delete(*Temp);
		Error = TEXT("파일 저장에 실패했습니다. 폴더 권한을 확인하세요.");
		return false;
	}
	Error.Reset();
	return true;
}
bool FLSAutoBattlePresetStore::Load(const FString& Name, FLSAutoBattlePreset& OutPreset, FString& Error)
{
	if (!IsValidName(Name))
	{
		Error = TEXT("프리셋 이름이 잘못되었습니다.");
		return false;
	}
	const FString Path = Filename(Name);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	FString Json;
	if (Size < 0 || Size > 1024 * 1024 || !FFileHelper::LoadFileToString(Json, *Path) || !Decode(Json, OutPreset))
	{
		Error = TEXT("파일이 없거나 형식·버전·수치가 잘못되었습니다.");
		return false;
	}
	Error.Reset();
	return true;
}
