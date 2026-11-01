#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace
{
void AdvanceSpeedWorld(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(0.005f, Seconds);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackSpeedApplicationTest, "Landship.Combat.BasicAttackTuning.ActualAttacks",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSAttackSpeedApplicationTest::RunTest(const FString& Parameters)
{
	for (float Multiplier : {0.5f, 1.0f, 2.0f})
	{
		auto* Config = GetMutableDefault<ULSCombatDeveloperSettings>();
		TGuardValue<float> Restore(Config->BasicAttackSpeedMultiplier, Multiplier);
		FTestWorldWrapper Play;
		if (!Play.CreateTestWorld(EWorldType::Game))
			return false;
		UWorld* World = Play.GetTestWorld();
		auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
		auto* Target = World->SpawnActor<ALSBasicAttackTestActor>(FVector(100, 0, 0), FRotator::ZeroRotator);
		Target->SetInitialFaction(ELSFaction::Enemy);
		FLSBasicAttackSettings Settings;
		Settings.Damage = 1;
		Settings.Interval = 0.2f;
		Settings.Range = 200;
		Source->Attack->SetAutoAttackOnBeginPlay(false);
		TestTrue(TEXT("시작 전 원본 설정 준비"), Source->Attack->ConfigureAttack(Settings));
		TestFalse(TEXT("배율 확정 전에는 실행 준비 미완료"), Source->Attack->IsConfigured());
		if (!Play.BeginPlayInTestWorld())
			return false;
		const float Expected = 0.2f / Multiplier;
		TestTrue(TEXT("배율 확정 후 실제 간격"),
		         FMath::IsNearlyEqual(Source->Attack->GetEffectiveAttackInterval(), Expected));
		TestTrue(TEXT("공격 시작"), Source->Attack->StartAttack(Target));
		TestEqual(TEXT("첫 타격"), Target->HitsReceived, 1);
		AdvanceSpeedWorld(World, Expected * 0.75f);
		TestEqual(TEXT("실제 간격 전 타격 없음"), Target->HitsReceived, 1);
		const float Remaining = Source->Attack->GetCooldownRemaining();
		TestTrue(TEXT("남은 시간도 실제 간격 기준"), Remaining > 0 && Remaining < Expected);
		TestTrue(TEXT("조회한 원본으로 재설정"), Source->Attack->ConfigureAttack(Source->Attack->GetAttackSettings()));
		TestTrue(TEXT("배율 중복 적용 없음"),
		         FMath::IsNearlyEqual(Source->Attack->GetEffectiveAttackInterval(), Expected));
		TestTrue(TEXT("재설정으로 진행 중 쿨타임 초기화 없음"),
		         FMath::IsNearlyEqual(Source->Attack->GetCooldownRemaining(), Remaining));
		AdvanceSpeedWorld(World, Expected * 0.5f);
		TestEqual(TEXT("실제 간격 후 두 번째 타격"), Target->HitsReceived, 2);
		TestEqual(TEXT("원본 간격 보존"), Source->Attack->GetAttackSettings().Interval, 0.2f);
		Source->Attack->StopAttack();
		Config->BasicAttackSpeedMultiplier = Multiplier * 3;
		auto* Late = World->SpawnActor<ALSBasicAttackTestActor>();
		Late->Attack->ConfigureAttack(Settings);
		TestTrue(TEXT("중간 생성도 같은 플레이 배율"),
		         FMath::IsNearlyEqual(Late->Attack->GetEffectiveAttackInterval(), Expected));
		Late->Attack->StartAttack(Target);
		const int32 Before = Target->HitsReceived;
		AdvanceSpeedWorld(World, Expected * 1.25f);
		TestEqual(TEXT("중간 생성 대상 실제 주기"), Target->HitsReceived, Before + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackSpeedInvalidApplicationTest,
                                 "Landship.Combat.BasicAttackTuning.RejectInvalidApplication",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSAttackSpeedInvalidApplicationTest::RunTest(const FString& Parameters)
{
	auto* Config = GetMutableDefault<ULSCombatDeveloperSettings>();
	TGuardValue<float> Restore(Config->BasicAttackSpeedMultiplier, 0.0f);
	AddExpectedMessagePlain(TEXT("Invalid BasicAttackSpeedMultiplier="), ELogVerbosity::Warning,
	                        EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Basic attack initialization rejected"), ELogVerbosity::Warning,
	                        EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Basic attack configuration rejected"), ELogVerbosity::Warning,
	                        EAutomationExpectedMessageFlags::Contains, 1);
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	auto* Source = Play.GetTestWorld()->SpawnActor<ALSBasicAttackTestActor>();
	FLSBasicAttackSettings Settings;
	Settings.Damage = 1;
	Settings.Range = 200;
	Source->Attack->ConfigureAttack(Settings);
	if (!Play.BeginPlayInTestWorld())
		return false;
	TestFalse(TEXT("잘못된 배율은 기본값으로 대체하지 않음"), Source->Attack->IsConfigured());
	TestEqual(TEXT("준비 실패의 실제 간격"), Source->Attack->GetEffectiveAttackInterval(), 0.0f);
	TestFalse(TEXT("진행 중 설정 요청도 거절"), Source->Attack->ConfigureAttack(Settings));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackSpeedExtremeTest,
                                 "Landship.Combat.BasicAttackTuning.RejectInvalidEffectiveInterval",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSAttackSpeedExtremeTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Basic attack configuration rejected"), ELogVerbosity::Warning,
	                        EAutomationExpectedMessageFlags::Contains, 2);
	for (bool bUnderflow : {false, true})
	{
		auto* Config = GetMutableDefault<ULSCombatDeveloperSettings>();
		TGuardValue<float> Restore(Config->BasicAttackSpeedMultiplier,
		                           bUnderflow ? std::numeric_limits<float>::max() : 1.e-30f);
		FTestWorldWrapper Play;
		if (!Play.CreateTestWorld(EWorldType::Game) || !Play.BeginPlayInTestWorld())
			return false;
		auto* Source = Play.GetTestWorld()->SpawnActor<ALSBasicAttackTestActor>();
		FLSBasicAttackSettings Settings;
		Settings.Damage = 1;
		Settings.Range = 200;
		Settings.Interval = bUnderflow ? 1.e-30f : std::numeric_limits<float>::max();
		TestFalse(TEXT("나눗셈 결과 0·무한 간격 거부"), Source->Attack->ConfigureAttack(Settings));
		TestFalse(TEXT("실패한 공격 실행 차단"), Source->Attack->IsConfigured());
	}
	return true;
}
#endif
