#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
#include "Troops/LSUnit.h"
#include "Enemies/LSOutpost.h"
#include <limits>

namespace
{
	void AdvanceAttackWorld(UWorld* World, float Duration)
	{
		for (float Remaining = Duration; Remaining > KINDA_SMALL_NUMBER;)
		{
			const float Step = FMath::Min(0.01f, Remaining);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			Remaining -= Step;
		}
	}

	FLSBasicAttackSettings TestSettings()
	{
		FLSBasicAttackSettings Settings;
		Settings.Damage = 10.0f;
		Settings.Interval = 0.2f;
		Settings.Range = 200.0f;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackLifecycleTest, "Landship.Combat.BasicAttack.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Ally = World->SpawnActor<ALSBasicAttackTestActor>();
	Enemy->Team = 1;
	Enemy->SetActorLocation(FVector(100, 0, 0));
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	Source->Attack->OnAttackPerformed.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
	TestFalse(TEXT("미설정 상태 공격 거부"), Source->Attack->StartAttack(Enemy));
	TestTrue(TEXT("설정"), Source->Attack->ConfigureAttack(TestSettings()));
	TestFalse(TEXT("자기 자신 거부"), Source->Attack->StartAttack(Source));
	TestFalse(TEXT("아군 거부"), Source->Attack->StartAttack(Ally));
	TestFalse(TEXT("null 거부"), Source->Attack->StartAttack(nullptr));
	TestTrue(TEXT("적 공격 시작"), Source->Attack->StartAttack(Enemy));
	TestEqual(TEXT("첫 공격 즉시 적용"), Enemy->Health, 90.0f);
	TestEqual(TEXT("피해 알림"), Source->Notifications, 1);
	Source->Attack->StopAttack();
	Source->Attack->StartAttack(Enemy);
	TestEqual(TEXT("중단 재시작으로 쿨타임 우회 불가"), Enemy->HitsReceived, 1);
	AdvanceAttackWorld(World, 0.12f);
	TestEqual(TEXT("간격 전 추가 피해 없음"), Enemy->HitsReceived, 1);
	AdvanceAttackWorld(World, 0.12f);
	TestEqual(TEXT("간격 후 두 번째 공격"), Enemy->HitsReceived, 2);
	Enemy->SetActorLocation(FVector(500, 0, 0));
	AdvanceAttackWorld(World, 0.3f);
	TestEqual(TEXT("사거리 밖 피해 없음"), Enemy->HitsReceived, 2);
	Enemy->SetActorLocation(FVector(100, 0, 0));
	Source->bReenterOnNotification = true;
	AdvanceAttackWorld(World, 0.06f);
	TestEqual(TEXT("재진입 공격 중복 없음"), Enemy->HitsReceived, 3);
	TestFalse(TEXT("알림 안 재진입 명령 거부"), Source->bReentrantAccepted);
	Enemy->Team = 0;
	AdvanceAttackWorld(World, 0.3f);
	TestNull(TEXT("진영 변경 시 공격 해제"), Source->Attack->GetAttackTarget());
	TestEqual(TEXT("진영 변경 후 피해 없음"), Enemy->HitsReceived, 3);
	Enemy->Team = 1;
	Enemy->Health = 5.0f;
	Source->Attack->StartAttack(Enemy);
	AdvanceAttackWorld(World, 0.3f);
	TestEqual(TEXT("치명타 이후 재공격 없음"), Enemy->HitsReceived, 4);
	TestNull(TEXT("사망 대상 해제"), Source->Attack->GetAttackTarget());
	Enemy->Health = 100.0f;
	Source->Attack->StartAttack(Enemy);
	Source->Health = 0.0f;
	const int32 HitsBeforeOwnerDeath = Enemy->HitsReceived;
	AdvanceAttackWorld(World, 0.3f);
	TestEqual(TEXT("공격자 사망 후 피해 없음"), Enemy->HitsReceived, HitsBeforeOwnerDeath);
	TestNull(TEXT("공격자 사망 시 대상 해제"), Source->Attack->GetAttackTarget());
	Source->Health = 100.0f;
	Source->Attack->StartAttack(Enemy);
	Enemy->Destroy();
	AdvanceAttackWorld(World, 0.3f);
	TestNull(TEXT("파괴된 대상 해제"), Source->Attack->GetAttackTarget());
	Source->Destroy();
	AdvanceAttackWorld(World, 0.3f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackSettingsTest, "Landship.Combat.BasicAttack.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackSettingsTest::RunTest(const FString& Parameters)
{
	ULSBasicAttackComponent* Attack = NewObject<ULSBasicAttackComponent>();
	const FLSBasicAttackSettings Valid = TestSettings();
	TestTrue(TEXT("정상 설정"), Attack->ConfigureAttack(Valid));
	FLSBasicAttackSettings Invalid = Valid;
	Invalid.Interval = 0;
	TestFalse(TEXT("0 간격 거부"), Attack->ConfigureAttack(Invalid));
	Invalid = Valid;
	Invalid.Damage = -1;
	TestFalse(TEXT("음수 피해 거부"), Attack->ConfigureAttack(Invalid));
	Invalid = Valid;
	Invalid.Range = -1;
	TestFalse(TEXT("음수 사거리 거부"), Attack->ConfigureAttack(Invalid));
	Invalid = Valid;
	Invalid.Damage = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN 거부"), Attack->ConfigureAttack(Invalid));
	Invalid = Valid;
	Invalid.Interval = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("무한대 거부"), Attack->ConfigureAttack(Invalid));
	TestEqual(TEXT("실패 시 기존 피해 유지"), Attack->GetAttackSettings().Damage, Valid.Damage);
	TestEqual(TEXT("실패 시 기존 간격 유지"), Attack->GetAttackSettings().Interval, Valid.Interval);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackAcquisitionTest, "Landship.Combat.BasicAttack.Acquisition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackAcquisitionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Near = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Far = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Ally = World->SpawnActor<ALSBasicAttackTestActor>();
	Near->Team = Far->Team = 1;
	Near->SetActorLocation(FVector(100, 0, 0));
	Far->SetActorLocation(FVector(150, 0, 0));
	Ally->SetActorLocation(FVector(20, 0, 0));
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	Source->Attack->ConfigureAttack(TestSettings());
	TestTrue(TEXT("자동 탐색 시작"), Source->Attack->StartAutoAttack());
	TestEqual(TEXT("가장 가까운 적 자동 공격"), Near->Health, 90.0f);
	TestEqual(TEXT("더 가까운 아군 제외"), Ally->Health, 100.0f);
	TestEqual(TEXT("먼 적은 아직 피해 없음"), Far->Health, 100.0f);
	Near->Health = 0;
	AdvanceAttackWorld(World, 0.25f);
	TestEqual(TEXT("사망 대상 대신 다음 적 선택"), Far->Health, 90.0f);
	Far->SetActorLocation(FVector(500, 0, 0));
	AdvanceAttackWorld(World, 0.3f);
	TestEqual(TEXT("범위 밖 대상 탐색 제외"), Far->Health, 90.0f);
	TestNull(TEXT("적이 없으면 대상 비움"), Source->Attack->GetAttackTarget());
	Far->SetActorLocation(FVector(150, 0, 0));
	AdvanceAttackWorld(World, 0.06f);
	TestEqual(TEXT("다시 진입한 적 자동 획득"), Far->Health, 80.0f);
	Source->Attack->Deactivate();
	TestNull(TEXT("비활성화 즉시 해제"), Source->Attack->GetAttackTarget());
	AdvanceAttackWorld(World, 0.4f);
	TestEqual(TEXT("비활성화 후 추가 피해 없음"), Far->Health, 80.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackStatConnectionTest, "Landship.Combat.BasicAttack.StatConnection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackStatConnectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UClass* UnitClass = LoadClass<ALSUnit>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C"));
	UClass* OutpostClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestOutpost.BP_TestOutpost_C"));
	if (!TestNotNull(TEXT("유닛 BP"), UnitClass) || !TestNotNull(TEXT("거점 BP"), OutpostClass)) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSUnit* Unit = World->SpawnActor<ALSUnit>(UnitClass);
	ALSOutpost* Outpost = World->SpawnActor<ALSOutpost>(OutpostClass, FVector(1000, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("유닛"), Unit) || !TestNotNull(TEXT("거점"), Outpost)) { return false; }
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	FLSUnitStatRow UnitStats;
	FLSOutpostStatRow OutpostStats;
	TestTrue(TEXT("유닛 행 조회"), Unit->TryGetStats(UnitStats));
	TestTrue(TEXT("거점 행 조회"), Outpost->TryGetStats(OutpostStats));
	TestEqual(TEXT("유닛 공격력 연결"), Unit->BasicAttack->GetAttackSettings().Damage, UnitStats.AttackPower);
	TestEqual(TEXT("유닛 공격 간격 연결"), Unit->BasicAttack->GetAttackSettings().Interval, UnitStats.AttackInterval);
	TestEqual(TEXT("유닛 사거리 연결"), Unit->BasicAttack->GetAttackSettings().Range, UnitStats.AttackRange);
	TestEqual(TEXT("거점 공격력 연결"), Outpost->BasicAttack->GetAttackSettings().Damage, OutpostStats.AttackPower);
	TestEqual(TEXT("거점 공격 간격 연결"), Outpost->BasicAttack->GetAttackSettings().Interval, OutpostStats.AttackInterval);
	TestEqual(TEXT("거점 사거리 연결"), Outpost->BasicAttack->GetAttackSettings().Range, OutpostStats.AttackRange);
	TestFalse(TEXT("미연결 대상은 공격 거부"), Unit->BasicAttack->StartAttack(Outpost));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
