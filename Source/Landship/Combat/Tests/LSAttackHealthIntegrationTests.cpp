#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Troops/LSUnit.h"
#include "Castle/LSCastle.h"
#include "Enemies/LSOutpost.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackHealthIntegrationTest, "Landship.Combat.BasicAttack.RealHealthIntegration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackHealthIntegrationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UClass* UnitClass = LoadClass<ALSUnit>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C"));
	UClass* CastleClass =
	    LoadClass<ALSCastle>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestCastle.BP_TestCastle_C"));
	UClass* OutpostClass =
	    LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestOutpost.BP_TestOutpost_C"));
	if (!TestNotNull(TEXT("실제 병력 BP"), UnitClass) || !TestNotNull(TEXT("실제 성 BP"), CastleClass) ||
	    !TestNotNull(TEXT("실제 거점 BP"), OutpostClass))
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSUnit* Source = World->SpawnActor<ALSUnit>(UnitClass, FVector(0, 0, 200), FRotator::ZeroRotator, SpawnParameters);
	ALSUnit* Enemy =
	    World->SpawnActor<ALSUnit>(UnitClass, FVector(150, 0, 200), FRotator::ZeroRotator, SpawnParameters);
	ALSCastle* Castle =
	    World->SpawnActor<ALSCastle>(CastleClass, FVector(0, 800, 200), FRotator::ZeroRotator, SpawnParameters);
	ALSOutpost* Outpost =
	    World->SpawnActor<ALSOutpost>(OutpostClass, FVector(0, 1600, 200), FRotator::ZeroRotator, SpawnParameters);
	if (!Source || !Enemy || !Castle || !Outpost)
	{
		AddError(TEXT("테스트 대상 생성 실패"));
		return false;
	}
	Source->SetInitialFaction(ELSFaction::Ally);
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Castle->SetInitialFaction(ELSFaction::Ally);
	Outpost->SetInitialFaction(ELSFaction::Enemy);
	Enemy->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
	Source->Orders->SetComponentTickEnabled(false);
	Enemy->Orders->SetComponentTickEnabled(false);
	Outpost->Orders->SetComponentTickEnabled(false);
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	// BeginPlay의 Tick 등록 이후 꺼야 명령 컴포넌트를 실제로 격리할 수 있다.
	Source->Orders->SetComponentTickEnabled(false);
	Enemy->Orders->SetComponentTickEnabled(false);
	Outpost->Orders->SetComponentTickEnabled(false);
	TestTrue(TEXT("아군 병력 조작 가능"), Source->Selection->CanPlayerControl());
	TestFalse(TEXT("적 병력은 정보 확인만 가능"), Enemy->Selection->CanPlayerControl());
	TestTrue(TEXT("성 체력 조회 연결"), ULSCombatTargetLibrary::IsCombatAlive(Castle));
	TestTrue(TEXT("거점 체력 조회 연결"), ULSCombatTargetLibrary::IsCombatAlive(Outpost));
	TestFalse(TEXT("같은 진영 성 자동 표적 제외"), ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Castle));
	TestTrue(TEXT("적 거점 자동 표적 허용"), ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Outpost));
	ULSBasicAttackComponent* Attack = Source->FindComponentByClass<ULSBasicAttackComponent>();
	FLSBasicAttackSettings Settings;
	Settings.Damage = 10;
	Settings.Range = 500;
	Settings.Interval = 0.1f;
	Attack->ConfigureAttack(Settings);
	ULSHealthComponent* EnemyHealth = Enemy->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot Before;
	EnemyHealth->TryGetHealth(Before);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.05f);
	}
	FLSHealthSnapshot WithoutOrders;
	EnemyHealth->TryGetHealth(WithoutOrders);
	TestEqual(TEXT("명령 처리 없이 기본 공격 타이머가 자동 시작되지 않음"), WithoutOrders.CurrentHealth,
	          Before.CurrentHealth);
	TestTrue(TEXT("실제 상대 진영 기본 공격 실행"), Attack->StartAttack(Enemy));
	FLSHealthSnapshot After;
	EnemyHealth->TryGetHealth(After);
	TestEqual(TEXT("팀원 체력 컴포넌트에서 피해 차감"), After.CurrentHealth, Before.CurrentHealth - 10);
	FLSHealthSnapshot DisplaySnapshot;
	TestTrue(TEXT("HUD와 동일한 공통 스냅샷 조회"), EnemyHealth->TryGetHealth(DisplaySnapshot));
	TestEqual(TEXT("HUD 체력과 실제 체력 일치"), DisplaySnapshot.CurrentHealth, After.CurrentHealth);
	TestEqual(TEXT("HUD 최대 체력 일치"), DisplaySnapshot.MaxHealth, After.MaxHealth);
	TestTrue(TEXT("사망 처리 전 적 공격 활성화"), Enemy->BasicAttack->StartAttack(Source));
	EnemyHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(After.MaxHealth, Source, Source));
	TestFalse(TEXT("사망 대상 선택 제외"), Enemy->Selection->IsSelectable());
	TestFalse(TEXT("사망 대상 이동 능력 차단"), Enemy->Orders->CanMove());
	TestFalse(TEXT("사망 대상 공격 능력 차단"), Enemy->Orders->CanAttack());
	TestFalse(TEXT("사망 후 공격 재시작 거부"), Attack->StartAttack(Enemy));
	TestNull(TEXT("사망 필수 처리에서 적의 공격 타이머 해제"),
	         Enemy->FindComponentByClass<ULSBasicAttackComponent>()->GetAttackTarget());
	TestTrue(TEXT("공격 측 표적 판정도 사망 제외"), !ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Enemy));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
