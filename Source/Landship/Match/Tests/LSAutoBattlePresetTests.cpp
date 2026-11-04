#if WITH_DEV_AUTOMATION_TESTS
#include "Match/LSAutoBattlePreset.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAutoBattlePresetTest, "Landship.AutoBattle.Preset.SerializationAndFiles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSAutoBattlePresetTest::RunTest(const FString& Parameters)
{
	FLSAutoBattlePreset P;
	FLSAutoBattlePlacement Unit;
	Unit.Type = ELSTroopType::Archer;
	Unit.Position = FVector(123.25, -456, 8);
	P.Allies.Add(Unit);
	P.Enemies.Add(Unit);
	P.AllyCastle = FVector(-1500, 0, 0);
	P.EnemyCastle = FVector(1700, 0, 0);
	FString Json;
	FLSAutoBattlePreset Read;
	TestTrue(TEXT("프리셋 JSON 인코딩"), FLSAutoBattlePresetStore::Encode(P, Json));
	TestTrue(TEXT("프리셋 JSON 디코딩"), FLSAutoBattlePresetStore::Decode(Json, Read));
	TestEqual(TEXT("병종 복원"), Read.Allies[0].Type, ELSTroopType::Archer);
	TestTrue(TEXT("좌표 소수값 보존"), Read.Allies[0].Position.Equals(Unit.Position));
	TestEqual(TEXT("일시적 액터 ID는 저장하지 않음"), Read.Allies[0].Id, 0);
	Read.Budget = 1234;
	TestFalse(TEXT("깨진 JSON 거부"), FLSAutoBattlePresetStore::Decode(TEXT("{"), Read));
	TestEqual(TEXT("파싱 실패 시 출력 유지"), Read.Budget, 1234);
	FString Bad = Json.Replace(TEXT("\"version\": 1"), TEXT("\"version\": 2"));
	TestFalse(TEXT("미지원 버전 거부"), FLSAutoBattlePresetStore::Decode(Bad, Read));
	Bad = Json.Replace(TEXT("\"Archer\""), TEXT("\"Unknown\""));
	TestFalse(TEXT("알 수 없는 병종 거부"), FLSAutoBattlePresetStore::Decode(Bad, Read));
	TestFalse(TEXT("필수 필드 누락 거부"), FLSAutoBattlePresetStore::Decode(TEXT("{}"), Read));
	P.Budget = 0;
	TestFalse(TEXT("편성 비용보다 작은 예산 거부"), P.IsValid());
	P.Budget = 1000;
	TestTrue(TEXT("한글 프리셋 이름 허용"), FLSAutoBattlePresetStore::IsValidName(TEXT("궁병 전열_테스트")));
	TestFalse(TEXT("상위 경로 거부"), FLSAutoBattlePresetStore::IsValidName(TEXT("../outside")));
	TestFalse(TEXT("절대 경로 거부"), FLSAutoBattlePresetStore::IsValidName(TEXT("C:/outside")));
	const FString Name = TEXT("Automation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Path = FPaths::Combine(FLSAutoBattlePresetStore::Directory(), TEXT("Preset_") + Name + TEXT(".json"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Path);
	};
	FString Error;
	TestTrue(TEXT("새 파일 저장"), FLSAutoBattlePresetStore::Save(Name, P, false, Error));
	P.Budget = 2000;
	TestFalse(TEXT("명시적 선택 없는 덮어쓰기 거부"), FLSAutoBattlePresetStore::Save(Name, P, false, Error));
	TestTrue(TEXT("이전 파일 읽기"), FLSAutoBattlePresetStore::Load(Name, Read, Error));
	TestEqual(TEXT("거부된 덮어쓰기는 이전 내용 유지"), Read.Budget, 1000);
	TestTrue(TEXT("명시적 덮어쓰기 허용"), FLSAutoBattlePresetStore::Save(Name, P, true, Error));
	TestTrue(TEXT("덮어쓴 파일 읽기"), FLSAutoBattlePresetStore::Load(Name, Read, Error));
	TestEqual(TEXT("새 내용 반영"), Read.Budget, 2000);
#if PLATFORM_WINDOWS
	{
		TUniquePtr<FArchive> LockedFile(IFileManager::Get().CreateFileReader(*Path));
		TestTrue(TEXT("교체 실패 검사 파일 잠금"), LockedFile.IsValid());
		P.Budget = 3000;
		TestFalse(TEXT("잠긴 파일 교체 실패"), FLSAutoBattlePresetStore::Save(Name, P, true, Error));
	}
	TestTrue(TEXT("교체 실패 뒤 이전 파일 읽기"), FLSAutoBattlePresetStore::Load(Name, Read, Error));
	TestEqual(TEXT("교체 실패는 이전 파일을 삭제하지 않음"), Read.Budget, 2000);
#endif
	TestTrue(TEXT("목록에 저장 파일 표시"), FLSAutoBattlePresetStore::List().Contains(Name));
	FFileHelper::SaveStringToFile(TEXT("broken"), *Path);
	TestFalse(TEXT("손상 파일 거부"), FLSAutoBattlePresetStore::Load(Name, Read, Error));
	TestEqual(TEXT("손상 파일은 기존 출력 유지"), Read.Budget, 2000);
	return true;
}
#endif
