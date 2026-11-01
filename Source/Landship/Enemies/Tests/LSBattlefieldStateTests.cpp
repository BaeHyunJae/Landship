#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSOutpost.h"
#include "Enemies/Tests/LSBattlefieldStateTestObserver.h"
#include "Castle/LSCastle.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"

namespace
{
void TickBattlefieldTimers(UWorld* World)
{
	++GFrameCounter;
	World->GetTimerManager().Tick(0.1f);
}

template <typename T>
T* SpawnBattlefieldActor(UWorld* World)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
}

bool PrepareCastleHealth(AActor* Castle)
{
	return Castle->FindComponentByClass<ULSHealthComponent>()->InitializeHealth(
		ELSCombatStatsInitResult::Success, 100.0f, FLSPrepareCombatDeath::CreateLambda([] {}), FString());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldCastleRepresentationsTest,
	"Landship.Enemies.Battlefield.CastleRepresentationsAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldCastleRepresentationsTest::RunTest(const FString& Parameters)
{
	for (const bool bMobile : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		AActor* Castle = bMobile ? static_cast<AActor*>(SpawnBattlefieldActor<ALSMobileCastle>(World))
			: static_cast<AActor*>(SpawnBattlefieldActor<ALSCastle>(World));
		ALSBattlefieldSetup* Setup = SpawnBattlefieldActor<ALSBattlefieldSetup>(World);
		Setup->PlayerCastle = Castle;
		Setup->CoreOutpost = SpawnBattlefieldActor<ALSOutpost>(World);
		ULSBattlefieldStateSubsystem* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
		if (!TestNotNull(TEXT("게임 월드 전장 상태"), State)) return false;
		TestTrue(TEXT("두 성 표현의 설정 검사"), Setup->GetConfigurationError().IsEmpty());
		State->OnWorldBeginPlay(*World);
		TestTrue(TEXT("체력 초기화 전에도 명시적 설정은 유효"), State->IsSetupValid());
		FLSPlayerCastleState Snapshot;
		Snapshot.Location = FVector(123, 456, 789);
		Snapshot.bIsDead = true;
		TestFalse(TEXT("준비되지 않은 체력 조회 거부"), State->TryGetPlayerCastleState(Snapshot));
		TestEqual(TEXT("실패 시 위치 출력 보존"), Snapshot.Location, FVector(123, 456, 789));
		TestTrue(TEXT("실패 시 사망 출력 보존"), !!Snapshot.bIsDead);
		if (!TestTrue(TEXT("BeginPlay 순서와 독립적으로 나중에 체력 준비"), PrepareCastleHealth(Castle))) return false;
		TestTrue(TEXT("준비 후 성 상태 조회"), State->TryGetPlayerCastleState(Snapshot));
		TestTrue(TEXT("명시적 성 정체성 유지"), Snapshot.Castle.Get() == Castle);
		TestEqual(TEXT("현재 위치 조회"), Snapshot.Location, Castle->GetActorLocation());
		TestFalse(TEXT("살아 있는 성"), !!Snapshot.bIsDead);
		FLSDamageRequest Damage;
		Damage.DamageAmount = 100;
		Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(Damage);
		TestTrue(TEXT("성 사망 관찰"), State->IsBattleEndedForEnemies());
		Castle->Destroy();
		TestTrue(TEXT("성 제거 후 사망 사실 유지"), State->IsBattleEndedForEnemies());
		State->Deinitialize();
		TestFalse(TEXT("종료 후 설정 참조 해제"), State->IsSetupValid());
		TestNull(TEXT("종료 후 핵심 참조 해제"), State->GetCoreOutpost());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldInvalidSetupTest,
	"Landship.Enemies.Battlefield.InvalidSetup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldInvalidSetupTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	FTestWorldWrapper OtherWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !OtherWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ULSBattlefieldStateSubsystem* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	State->OnWorldBeginPlay(*World);
	TestFalse(TEXT("설정 누락은 실패"), State->IsSetupValid());
	ALSBattlefieldSetup* Setup = SpawnBattlefieldActor<ALSBattlefieldSetup>(World);
	TestFalse(TEXT("참조 누락은 실패"), Setup->GetConfigurationError().IsEmpty());
	Setup->PlayerCastle = SpawnBattlefieldActor<ALSCastle>(OtherWorld.GetTestWorld());
	Setup->CoreOutpost = SpawnBattlefieldActor<ALSOutpost>(World);
	TestTrue(TEXT("다른 월드 참조 진단"), Setup->GetConfigurationError().Contains(TEXT("world")));
	Setup->PlayerCastle = SpawnBattlefieldActor<ALSCastle>(World);
	Setup->CoreOutpost->SetInitialFaction(ELSFaction::Ally);
	TestTrue(TEXT("아군 핵심 거점 거부"), Setup->GetConfigurationError().Contains(TEXT("Enemy")));
	Setup->CoreOutpost->SetInitialFaction(ELSFaction::Enemy);
	ULSHealthComponent* ExtraHealth = NewObject<ULSHealthComponent>(Setup->PlayerCastle);
	Setup->PlayerCastle->AddInstanceComponent(ExtraHealth);
	ExtraHealth->RegisterComponent();
	TestTrue(TEXT("중복 성 체력 거부"), Setup->GetConfigurationError().Contains(TEXT("exactly one")));
	ExtraHealth->DestroyComponent();
	FTestWorldWrapper DuplicateWorld;
	if (!DuplicateWorld.CreateTestWorld(EWorldType::Game)) return false;
	SpawnBattlefieldActor<ALSBattlefieldSetup>(DuplicateWorld.GetTestWorld());
	SpawnBattlefieldActor<ALSBattlefieldSetup>(DuplicateWorld.GetTestWorld());
	ULSBattlefieldStateSubsystem* DuplicateState = DuplicateWorld.GetTestWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	DuplicateState->OnWorldBeginPlay(*DuplicateWorld.GetTestWorld());
	TestFalse(TEXT("중복 설정은 임의로 하나를 고르지 않음"), DuplicateState->IsSetupValid());
	TestNull(TEXT("실패 시 핵심 거점 미노출"), DuplicateState->GetCoreOutpost());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldCoreCaptureTest,
	"Landship.Enemies.Battlefield.CoreCaptureSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldCoreCaptureTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnBattlefieldActor<ALSBattlefieldSetup>(World);
	Setup->PlayerCastle = SpawnBattlefieldActor<ALSCastle>(World);
	Setup->CoreOutpost = SpawnBattlefieldActor<ALSOutpost>(World);
	ALSOutpost* Core = Setup->CoreOutpost;
	const FName OriginalName = Core->GetFName();
	const FVector OriginalLocation = Core->GetActorLocation();
	ULSBattlefieldStateSubsystem* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	State->OnWorldBeginPlay(*World);
	ULSBattlefieldStateTestObserver* Observer = NewObject<ULSBattlefieldStateTestObserver>(State);
	State->OnCoreOutpostCapturedEvent.AddDynamic(Observer, &ULSBattlefieldStateTestObserver::OnCaptured);
	FLSOutpostCaptureSnapshot Capture;
	Capture.OutpostName = TEXT("Unchanged");
	TestFalse(TEXT("함락 전 조회 실패"), State->TryGetCoreCapture(Capture));
	TestEqual(TEXT("실패 조회 출력 보존"), Capture.OutpostName, FName(TEXT("Unchanged")));
	ALSOutpost* Ordinary = SpawnBattlefieldActor<ALSOutpost>(World);
	State->RecordOutpostCaptured(Ordinary);
	TestFalse(TEXT("일반 거점 함락은 전투 종료가 아님"), State->IsBattleEndedForEnemies());
	State->RecordOutpostCaptured(Core);
	TestEqual(TEXT("필수 정리 호출 스택에서는 알림 없음"), Observer->Captures.Num(), 0);
	TestTrue(TEXT("콜백 반환 전 함락 사실 조회"), State->TryGetCoreCapture(Capture));
	TestTrue(TEXT("핵심 함락은 적 종료 대기 조건"), State->IsBattleEndedForEnemies());
	Core->Destroy();
	State->RecordOutpostCaptured(Core);
	TickBattlefieldTimers(World);
	TestEqual(TEXT("제거 뒤에도 다음 틱에 한 번 알림"), Observer->Captures.Num(), 1);
	State->RecordOutpostCaptured(Core);
	TickBattlefieldTimers(World);
	TestEqual(TEXT("반복 기록과 틱으로 알림이 늘지 않음"), Observer->Captures.Num(), 1);
	TestTrue(TEXT("액터 제거와 지연 통지 뒤 늦은 조회"), State->TryGetCoreCapture(Capture));
	TestEqual(TEXT("함락 당시 이름 보존"), Capture.OutpostName, OriginalName);
	TestEqual(TEXT("함락 당시 위치 보존"), Capture.Location, OriginalLocation);
	TestTrue(TEXT("핵심 여부 보존"), !!Capture.bIsCoreOutpost);
	State->Deinitialize();
	TickBattlefieldTimers(World);
	TestEqual(TEXT("종료 뒤 알림 없음"), Observer->Captures.Num(), 1);
	TestFalse(TEXT("종료 후 스냅샷 해제"), State->TryGetCoreCapture(Capture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldCaptureTeardownTest,
	"Landship.Enemies.Battlefield.CaptureTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldCaptureTeardownTest::RunTest(const FString& Parameters)
{
	for (const bool bRecordCapture : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SpawnBattlefieldActor<ALSBattlefieldSetup>(World);
		Setup->PlayerCastle = SpawnBattlefieldActor<ALSCastle>(World);
		Setup->CoreOutpost = SpawnBattlefieldActor<ALSOutpost>(World);
		ULSBattlefieldStateSubsystem* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
		State->OnWorldBeginPlay(*World);
		ULSBattlefieldStateTestObserver* Observer = NewObject<ULSBattlefieldStateTestObserver>(State);
		State->OnCoreOutpostCapturedEvent.AddDynamic(Observer, &ULSBattlefieldStateTestObserver::OnCaptured);
		if (bRecordCapture)
		{
			State->RecordOutpostCaptured(Setup->CoreOutpost);
		}
		Setup->CoreOutpost->Destroy();
		if (!bRecordCapture)
		{
			FLSOutpostCaptureSnapshot Capture;
			TestFalse(TEXT("액터 제거만으로 함락을 만들지 않음"), State->TryGetCoreCapture(Capture));
			TestFalse(TEXT("액터 제거만으로 적 전투를 종료하지 않음"), State->IsBattleEndedForEnemies());
		}
		State->Deinitialize();
		TickBattlefieldTimers(World);
		TestEqual(TEXT("종료 시 대기 중 함락 타이머 취소"), Observer->Captures.Num(), 0);
	}
	return true;
}

#endif
