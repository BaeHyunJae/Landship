#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
void AdvanceExecutionWorld(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}

void ConfigureExecutionAttack(ALSBasicAttackTestActor* Source, float Damage = 10.0f)
{
	FLSBasicAttackSettings Settings;
	Settings.Damage = Damage;
	Settings.Interval = 0.2f;
	Settings.Range = 200.0f;
	Source->Attack->ConfigureAttack(Settings);
	Source->Attack->OnAttackExecutedEvent.AddUniqueDynamic(Source, &ALSBasicAttackTestActor::ObserveExecution);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackExecutionCommitTest, "Landship.Combat.Execution.Commit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackExecutionCommitTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Enemy->SetActorLocation(FVector(500, 0, 0));
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureExecutionAttack(Source, 0);
	Source->Attack->OnAttackPerformed.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
	TestTrue(TEXT("범위 밖 명령 허용"), Source->Attack->StartAttack(Enemy));
	TestEqual(TEXT("명령만으로 확정 알림 없음"), Source->Executions.Num(), 0);
	AdvanceExecutionWorld(World, 0.1f);
	TestEqual(TEXT("범위 밖 알림 없음"), Source->Executions.Num(), 0);
	Enemy->SetActorLocation(FVector(100, 0, 0));
	Source->bReenterOnExecution = true;
	AdvanceExecutionWorld(World, 0.06f);
	TestEqual(TEXT("피해 0 시도도 한 번 확정"), Source->Executions.Num(), 1);
	TestEqual(TEXT("기존 피해 알림은 발생하지 않음"), Source->Notifications, 0);
	TestFalse(TEXT("확정 콜백 재진입 거부"), Source->bReentrantAccepted);
	TestEqual(TEXT("피해 없음"), Enemy->GetCurrentHealth(), 100.0f);
	if (Source->Executions.Num() == 1)
	{
		const FLSAttackExecution& Execution = Source->Executions[0];
		TestEqual(TEXT("소스"), Execution.Source.Get(), static_cast<AActor*>(Source));
		TestEqual(TEXT("대상"), Execution.Target.Get(), static_cast<AActor*>(Enemy));
		TestEqual(TEXT("위치 스냅샷"), Execution.TargetLocation, FVector(100, 0, 0));
		TestFalse(TEXT("근접"), Execution.bProjectile);
		TestEqual(TEXT("첫 식별자"), Execution.ExecutionId, int64(1));
	}
	Source->Attack->StopAttack();
	ConfigureExecutionAttack(Source);
	Source->Attack->StartAttack(Enemy);
	TestEqual(TEXT("재시작 시 쿨타임 유지"), Source->Executions.Num(), 1);
	AdvanceExecutionWorld(World, 0.22f);
	TestEqual(TEXT("쿨타임 이후 한 번"), Source->Executions.Num(), 2);
	TestEqual(TEXT("확정 시점은 피해 전"), Source->HealthAtExecution, 100.0f);
	TestEqual(TEXT("미바인딩 기존 피해 경로"), Enemy->GetCurrentHealth(), 90.0f);
	Source->Attack->StopAttack();
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackExecutionResolverTest, "Landship.Combat.Execution.Resolver",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackExecutionResolverTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* First = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Next = World->SpawnActor<ALSBasicAttackTestActor>();
	First->SetInitialFaction(ELSFaction::Enemy);
	Next->SetInitialFaction(ELSFaction::Enemy);
	First->SetActorLocation(FVector(100, 0, 0));
	Next->SetActorLocation(FVector(150, 0, 0));
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureExecutionAttack(Source);
	AActor* Selected = First;
	int32 Resolutions = 0;
	Source->Attack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateLambda([&]() -> AActor*
	{
		++Resolutions;
		TestFalse(TEXT("해결 콜백 재진입 거부"), Source->Attack->StartAutoAttack());
		return Selected;
	}));
	Source->Attack->StartAttack(First);
	Selected = Next;
	AdvanceExecutionWorld(World, 0.1f);
	TestEqual(TEXT("쿨타임 중 선택 함수 호출 없음"), Resolutions, 1);
	TestEqual(TEXT("쿨타임 중 피해 없음"), Next->GetCurrentHealth(), 100.0f);
	AdvanceExecutionWorld(World, 0.12f);
	TestEqual(TEXT("타격 직전 새 대상 선택"), Next->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("이전 대상 추가 피해 없음"), First->GetCurrentHealth(), 90.0f);
	Source->Attack->StopAttack();
	Selected = nullptr;
	AdvanceExecutionWorld(World, 0.25f);
	Source->Attack->StartAttack(First);
	TestEqual(TEXT("null 선택은 공격 없음"), Source->Executions.Num(), 2);
	TestEqual(TEXT("null 선택은 쿨타임 소비 없음"), Source->Attack->GetCooldownRemaining(), 0.0f);
	Selected = Next;
	AdvanceExecutionWorld(World, 0.06f);
	TestEqual(TEXT("중단 후에도 선택 함수 유지"), Next->GetCurrentHealth(), 80.0f);
	Source->Attack->StopAttack();
	AdvanceExecutionWorld(World, 0.25f);
	Source->Attack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateLambda([&]() -> AActor*
	{
		Source->Attack->StopAttack();
		return First;
	}));
	Source->Attack->StartAttack(First);
	TestEqual(TEXT("선택 중 중단 명령은 공격 취소"), Source->Executions.Num(), 3);
	Source->Attack->SetAttackTargetResolver(FLSResolveAttackTarget());
	Source->bDestroyTargetOnExecution = true;
	Source->Attack->StartAttack(First);
	TestEqual(TEXT("대상 제거 시도는 한 번 기록"), Source->Executions.Num(), 4);
	AdvanceExecutionWorld(World, 0.25f);
	TestEqual(TEXT("제거된 대상 재공격 없음"), Source->Executions.Num(), 4);
	Source->bDestroySourceOnExecution = true;
	Source->bDestroyTargetOnExecution = false;
	Source->Attack->StartAttack(Next);
	TestTrue(TEXT("콜백 소스 제거 완료"), Source->IsActorBeingDestroyed());
	TestEqual(TEXT("소스 제거 후 피해 없음"), Next->GetCurrentHealth(), 80.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackExecutionProjectileTest, "Landship.Combat.Execution.Projectile",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackExecutionProjectileTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Source->SetActorLocation(FVector(0, 0, 100));
	Enemy->SetActorLocation(FVector(150, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureExecutionAttack(Source);
	FLSProjectileAttackSettings ProjectileSettings;
	ProjectileSettings.ProjectileClass = ALSBasicProjectile::StaticClass();
	ProjectileSettings.Speed = 300;
	Source->Attack->ConfigureProjectile(ProjectileSettings);
	Source->Attack->StartAttack(Enemy);
	Source->Attack->StopAttack();
	TestEqual(TEXT("발사 즉시 한 번"), Source->Executions.Num(), 1);
	TestEqual(TEXT("발사는 피해와 구분"), Enemy->GetCurrentHealth(), 100.0f);
	if (Source->Executions.Num() == 1)
	{
		TestTrue(TEXT("투사체 표시"), Source->Executions[0].bProjectile);
	}
	AdvanceExecutionWorld(World, 0.7f);
	TestEqual(TEXT("실제 명중"), Enemy->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("명중 시 확정 알림 중복 없음"), Source->Executions.Num(), 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
