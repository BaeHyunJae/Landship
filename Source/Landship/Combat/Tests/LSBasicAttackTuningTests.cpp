#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackTuningSubsystem.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackTuningSettingsTest, "Landship.Combat.BasicAttackTuning.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackTuningSettingsTest::RunTest(const FString& Parameters)
{
	// 실험 중인 저장값 대신 명시한 1배 설정으로 기본 동작을 확인한다.
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	TGuardValue<float> RestoreMultiplier(Settings->BasicAttackSpeedMultiplier, 1.0f);
	FTestWorldWrapper Play;
	if (!TestTrue(TEXT("기본 배율 확인용 월드 생성"), Play.CreateTestWorld(EWorldType::Game)) ||
		!TestTrue(TEXT("기본 배율 확인용 플레이 시작"), Play.BeginPlayInTestWorld()))
	{
		return false;
	}
	float Multiplier = 0.0f;
	TestTrue(TEXT("기본 배율 조회 성공"),
		Play.GetTestWorld()->GetSubsystem<ULSBasicAttackTuningSubsystem>()->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("설정한 1배를 조회"), Multiplier, 1.0f);
	const FFloatProperty* Property = FindFProperty<FFloatProperty>(ULSCombatDeveloperSettings::StaticClass(),
		GET_MEMBER_NAME_CHECKED(ULSCombatDeveloperSettings, BasicAttackSpeedMultiplier));
	if (!TestNotNull(TEXT("설정 항목이 리플렉션에 등록됨"), Property))
	{
		return false;
	}
	TestTrue(TEXT("설정으로 저장 가능"), Property->HasAnyPropertyFlags(CPF_Config));
	TestTrue(TEXT("에디터에서 편집 가능"), Property->HasAnyPropertyFlags(CPF_Edit));
	TestEqual(TEXT("에디터 입력 하한"), Property->GetMetaData(TEXT("ClampMin")), FString(TEXT("0.0")));
	TestFalse(TEXT("실험용 상한을 고정하지 않음"), Property->HasMetaData(TEXT("ClampMax")));
	TestTrue(TEXT("블루프린트에서 조회 대상 타입 사용 가능"),
		ULSBasicAttackTuningSubsystem::StaticClass()->HasMetaData(TEXT("BlueprintType")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackTuningSnapshotTest, "Landship.Combat.BasicAttackTuning.PlaySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackTuningSnapshotTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	TGuardValue<float> RestoreMultiplier(Settings->BasicAttackSpeedMultiplier, 2.0f);
	FTestWorldWrapper FirstPlay;
	if (!TestTrue(TEXT("첫 게임 월드 생성"), FirstPlay.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* FirstWorld = FirstPlay.GetTestWorld();
	ULSBasicAttackTuningSubsystem* FirstTuning = FirstWorld->GetSubsystem<ULSBasicAttackTuningSubsystem>();
	if (!TestNotNull(TEXT("게임 월드에 자동 생성"), FirstTuning))
	{
		return false;
	}
	float Multiplier = 123.0f;
	TestFalse(TEXT("시작 전 조회 실패"), FirstTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("실패 시 출력 보존"), Multiplier, 123.0f);
	if (!TestTrue(TEXT("첫 플레이 시작"), FirstPlay.BeginPlayInTestWorld()))
	{
		return false;
	}
	TestTrue(TEXT("시작 후 조회 성공"), FirstTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("시작 시 지정한 2배 조회"), Multiplier, 2.0f);

	Settings->BasicAttackSpeedMultiplier = 0.5f;
	TestTrue(TEXT("설정 변경 후에도 조회 가능"), FirstTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("진행 중에는 시작 시 값 유지"), Multiplier, 2.0f);
	AActor* LateActor = FirstWorld->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("중간 생성 대상"), LateActor))
	{
		return false;
	}
	TestTrue(TEXT("중간 생성 대상도 같은 값 조회"),
		LateActor->GetWorld()->GetSubsystem<ULSBasicAttackTuningSubsystem>()->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("중간 생성에도 2배"), Multiplier, 2.0f);

	FTestWorldWrapper NextPlay;
	if (!TestTrue(TEXT("다음 게임 월드 생성"), NextPlay.CreateTestWorld(EWorldType::Game)) ||
		!TestTrue(TEXT("다음 플레이 시작"), NextPlay.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSBasicAttackTuningSubsystem* NextTuning = NextPlay.GetTestWorld()->GetSubsystem<ULSBasicAttackTuningSubsystem>();
	TestTrue(TEXT("다음 플레이 조회 성공"), NextTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("다음 플레이에 변경값 반영"), Multiplier, 0.5f);
	TestTrue(TEXT("다른 월드 시작 후에도 기존 값 조회"), FirstTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("월드 사이 값 분리"), Multiplier, 2.0f);
	FirstTuning->Deinitialize();
	Multiplier = 123.0f;
	TestFalse(TEXT("종료 후 조회 실패"), FirstTuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	TestEqual(TEXT("종료 후에도 실패 시 출력 보존"), Multiplier, 123.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackTuningInvalidSettingsTest, "Landship.Combat.BasicAttackTuning.InvalidSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackTuningInvalidSettingsTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	TGuardValue<float> RestoreMultiplier(Settings->BasicAttackSpeedMultiplier, 1.0f);
	const float InvalidValues[] = {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()};
	AddExpectedMessagePlain(TEXT("Invalid BasicAttackSpeedMultiplier="), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, UE_ARRAY_COUNT(InvalidValues));
	for (float InvalidValue : InvalidValues)
	{
		Settings->BasicAttackSpeedMultiplier = InvalidValue;
		FTestWorldWrapper Play;
		if (!TestTrue(TEXT("잘못된 설정에서도 월드 생성"), Play.CreateTestWorld(EWorldType::Game)) ||
			!TestTrue(TEXT("진단 후 플레이 시작"), Play.BeginPlayInTestWorld()))
		{
			return false;
		}
		float Multiplier = 123.0f;
		ULSBasicAttackTuningSubsystem* Tuning = Play.GetTestWorld()->GetSubsystem<ULSBasicAttackTuningSubsystem>();
		TestFalse(TEXT("잘못된 설정 조회 실패"), Tuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
		TestEqual(TEXT("1배로 대체하지 않고 출력 보존"), Multiplier, 123.0f);
		Settings->BasicAttackSpeedMultiplier = 1.0f;
		TestFalse(TEXT("진행 중 설정을 고쳐도 확정한 실패 유지"), Tuning->TryGetBasicAttackSpeedMultiplier(Multiplier));
	}
	return true;
}

#endif
