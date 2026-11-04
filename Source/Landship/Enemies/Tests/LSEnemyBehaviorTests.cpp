#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/Tests/LSBattlefieldStateTestObserver.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"
#include "Misc/DataValidation.h"

namespace
{
template <typename T>
T* SpawnEnemyTestActor(UWorld* World, const FVector& Location)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Parameters);
}

void AdvanceEnemyWorld(UWorld* World, float Duration)
{
	while (Duration > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Duration, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Duration -= Step;
	}
}

float EnemyTestHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>();
	return Health && Health->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

float DealEnemyTestDamage(AActor* Target, AActor* Source, float Damage)
{
	return Target->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(Damage, Source, Source)).AppliedDamage;
}

ALSBattlefieldSetup* SetupEnemyTestBattlefield(UWorld* World)
{
	ALSBattlefieldSetup* Setup = SpawnEnemyTestActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(10000, 0, 100));
	Castle->GetMovementComponent()->Deactivate();

	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = SpawnEnemyTestActor<ALSEnemyOutpost>(World, FVector(0, 0, 100));
	Setup->CoreOutpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
	return Setup;
}

void ConfigureEnemyTestAttack(ULSBasicAttackComponent* Attack)
{
	FLSBasicAttackSettings Settings;
	Settings.Damage = 10;
	Settings.Interval = 0.3f;
	Settings.Range = 300;
	Attack->ConfigureAttack(Settings);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyNativeCaptureIntegrationTest,
	"Landship.Enemies.Behavior.NativeCaptureIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyNativeCaptureIntegrationTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("기존 공용 병종에는 적 행동을 추가하지 않음"),
		GetDefault<ALSTroopUnit>()->FindComponentByClass<ULSEnemyBehaviorComponent>());
	TestNull(TEXT("기존 공용 거점에는 적 행동을 추가하지 않음"),
		GetDefault<ALSOutpost>()->FindComponentByClass<ULSEnemyBehaviorComponent>());
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
	ALSEnemyOutpost* Core = CastChecked<ALSEnemyOutpost>(Setup->CoreOutpost);
	const FName CoreName = Core->GetFName();
	const FVector CoreLocation = Core->GetActorLocation();
	ULSBattlefieldStateSubsystem* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	ULSBattlefieldStateTestObserver* Observer = NewObject<ULSBattlefieldStateTestObserver>(State);
	State->OnCoreOutpostCapturedEvent.AddDynamic(Observer, &ULSBattlefieldStateTestObserver::OnCaptured);
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	TestTrue(TEXT("실제 BeginPlay에서 전장 설정 연결"), State->IsSetupValid());
	TestTrue(TEXT("네이티브 거점 수치 초기화"), Core->IsStatsInitialized());
	TestTrue(TEXT("네이티브 성 체력 초기화"), EnemyTestHealth(Setup->PlayerCastle) > 0);
	const float Health = EnemyTestHealth(Core);
	if (!TestTrue(TEXT("네이티브 거점 체력 준비"), Health > 0)) return false;
	TestEqual(TEXT("실제 치명 피해 적용"), DealEnemyTestDamage(Core, Setup->PlayerCastle, Health), Health);
	TestTrue(TEXT("체력 0이 함락으로 연결"), Core->HasBeenCaptured());
	FLSOutpostCaptureSnapshot Capture;
	TestTrue(TEXT("정리 콜백 반환 전 월드 함락 조회"), State->TryGetCoreCapture(Capture));
	TestEqual(TEXT("동기 호출 스택에 알림 없음"), Observer->Captures.Num(), 0);
	AdvanceEnemyWorld(World, 0.05f);
	TestEqual(TEXT("실제 피해 뒤 함락 알림 한 번"), Observer->Captures.Num(), 1);
	TestTrue(TEXT("핵심 함락으로 적 전투 종료"), State->IsBattleEndedForEnemies());
	TestEqual(TEXT("함락 이름 보존"), Capture.OutpostName, CoreName);
	TestEqual(TEXT("함락 위치 보존"), Capture.Location, CoreLocation);
	TestTrue(TEXT("핵심 식별"), !!Capture.bIsCoreOutpost);
	AdvanceEnemyWorld(World, 0.5f);
	TestEqual(TEXT("추가 틱에도 중복 알림 없음"), Observer->Captures.Num(), 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyTroopStrikeSelectionTest,
	"Landship.Enemies.Behavior.TroopStrikeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyTroopStrikeSelectionTest::RunTest(const FString& Parameters)
{
	for (const ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Archer})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
		Setup->CoreOutpost->FindComponentByClass<ULSEnemyBehaviorComponent>()->SetComponentTickEnabled(false);
		Setup->CoreOutpost->SetActorLocation(FVector(0, -1000, 100));
		ALSEnemyTroop* Troop = SpawnEnemyTestActor<ALSEnemyTroop>(World, FVector(0, 0, 100));
		if (!TestTrue(TEXT("근접·원거리 병종 지정"), Troop->SetInitialTroopType(Type))) return false;
		Troop->GetCharacterMovement()->DisableMovement();
		Troop->Behavior->GuardedOutpost = Setup->CoreOutpost;
		Troop->BasicAttack->SetAutoAttackOnBeginPlay(false);
		ALSBasicAttackTestActor* Near = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(100, 80, 100));
		ALSBasicAttackTestActor* Low = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(200, -100, 100));
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		// BeginPlay의 Tick 등록 이후 격리하고, 거점과 병력의 초기 충돌도 분리한다.
		Setup->CoreOutpost->FindComponentByClass<ULSEnemyBehaviorComponent>()->SetComponentTickEnabled(false);
		Setup->CoreOutpost->BasicAttack->Deactivate();
		TestTrue(TEXT("공용 병종 수치 준비"), Troop->IsStatsInitialized());
		TestEqual(TEXT("적 전용 클래스의 진영"), Troop->GetFaction(), ELSFaction::Enemy);
		ConfigureEnemyTestAttack(Troop->BasicAttack);
		if (Type == ELSTroopType::Archer)
		{
			FLSProjectileAttackSettings Projectile = Troop->BasicAttack->GetProjectileSettings();
			Projectile.Speed = 10000;
			Troop->BasicAttack->ConfigureProjectile(Projectile);
		}
		DealEnemyTestDamage(Low, Troop, 40);
		AdvanceEnemyWorld(World, 0.08f);
		TestEqual(TEXT("더 먼 저체력 상대 우선"), Troop->BasicAttack->GetAttackTarget(), static_cast<AActor*>(Low));
		TestEqual(TEXT("저체력 상대 첫 타격"), EnemyTestHealth(Low), 50.0f);
		TestEqual(TEXT("가까운 상대는 아직 피해 없음"), EnemyTestHealth(Near), 100.0f);
		// 다음 행동 판단을 늦춰 기본 공격의 타격 직전 선택만 검사한다.
		Troop->Behavior->SetComponentTickEnabled(false);
		DealEnemyTestDamage(Near, Troop, 70);
		AdvanceEnemyWorld(World, 0.15f);
		TestEqual(TEXT("체력 역전으로 쿨타임 초기화 없음"), EnemyTestHealth(Near), 30.0f);
		AdvanceEnemyWorld(World, 0.18f);
		TestEqual(TEXT("다음 실제 타격에서 저체력 상대 재선택"), EnemyTestHealth(Near), 20.0f);
		TestEqual(TEXT("이전 상대에게 중복 타격 없음"), EnemyTestHealth(Low), 50.0f);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyStationaryAndIncomingTest,
	"Landship.Enemies.Behavior.StationaryAndIncoming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyStationaryAndIncomingTest::RunTest(const FString& Parameters)
{
	for (const bool bArmed : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
		ALSEnemyOutpost* Outpost = CastChecked<ALSEnemyOutpost>(Setup->CoreOutpost);
		FBoolProperty* ArmedProperty = FindFProperty<FBoolProperty>(ALSOutpost::StaticClass(), TEXT("bArmed"));
		if (!TestNotNull(TEXT("에디터 무장 설정 필드"), ArmedProperty)) return false;
		ArmedProperty->SetPropertyValue_InContainer(Outpost, bArmed);
		ALSBasicAttackTestActor* Near = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(100, 0, 100));
		ALSBasicAttackTestActor* Low = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(200, 0, 100));
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		ConfigureEnemyTestAttack(Outpost->BasicAttack);
		DealEnemyTestDamage(Low, Outpost, 40);
		const FVector InitialLocation = Outpost->GetActorLocation();
		const float AttackerHealth = EnemyTestHealth(Near);
		DealEnemyTestDamage(Outpost, Near, 1);
		TestEqual(TEXT("피격 콜백 안에서 동기 반격 없음"), EnemyTestHealth(Near), AttackerHealth);
		AdvanceEnemyWorld(World, 0.02f);
		TestEqual(TEXT("고정 거점 위치 유지"), Outpost->GetActorLocation(), InitialLocation);
		TestEqual(TEXT("가까운 상대보다 저체력 상대 우선"), EnemyTestHealth(Near), 100.0f);
		TestEqual(TEXT("무장 여부에 따라 실제 공격 구분"), EnemyTestHealth(Low), bArmed ? 50.0f : 60.0f);
		if (!bArmed)
		{
			TestNull(TEXT("비무장 거점 공격 대상 없음"), Outpost->BasicAttack->GetAttackTarget());
			AdvanceEnemyWorld(World, 0.5f);
			TestEqual(TEXT("비무장 거점은 이후에도 피해를 주지 않음"), EnemyTestHealth(Low), 60.0f);
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyBattleTerminationTest,
	"Landship.Enemies.Behavior.BattleTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyBattleTerminationTest::RunTest(const FString& Parameters)
{
	for (const bool bCoreCapture : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
		ALSEnemyOutpost* Survivor = SpawnEnemyTestActor<ALSEnemyOutpost>(World, FVector(3000, 0, 100));
		Survivor->BasicAttack->SetAutoAttackOnBeginPlay(false);
		ALSBasicAttackTestActor* Opponent = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(3100, 0, 100));
		ALSEnemyTroop* Troop = SpawnEnemyTestActor<ALSEnemyTroop>(World, FVector(3000, 0, 100));
		Troop->GetCharacterMovement()->DisableMovement();
		Troop->Behavior->GuardedOutpost = Survivor;
		Troop->BasicAttack->SetAutoAttackOnBeginPlay(false);
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		ConfigureEnemyTestAttack(Survivor->BasicAttack);
		ConfigureEnemyTestAttack(Troop->BasicAttack);
		AdvanceEnemyWorld(World, 0.02f);
		TestTrue(TEXT("종료 전 실제 적 공격"), EnemyTestHealth(Opponent) < 100);
		const float BeforeEnd = EnemyTestHealth(Opponent);
		AActor* EndTarget = bCoreCapture ? static_cast<AActor*>(Setup->CoreOutpost) : Setup->PlayerCastle.Get();
		AActor* EndSource = bCoreCapture ? Setup->PlayerCastle.Get() : static_cast<AActor*>(Survivor);
		DealEnemyTestDamage(EndTarget, EndSource, EnemyTestHealth(EndTarget));
		AdvanceEnemyWorld(World, 0.05f);
		TestEqual(TEXT("거점 종료 상태"), Survivor->Behavior->GetState(), ELSEnemyState::Ended);
		TestEqual(TEXT("분대 종료 상태"), Troop->Behavior->GetState(), ELSEnemyState::Ended);
		TestNull(TEXT("거점 타격 대상 해제"), Survivor->BasicAttack->GetAttackTarget());
		TestNull(TEXT("분대 타격 대상 해제"), Troop->BasicAttack->GetAttackTarget());
		AdvanceEnemyWorld(World, 0.5f);
		TestEqual(TEXT("종료 뒤 추가 공격 없음"), EnemyTestHealth(Opponent), BeforeEnd);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyInvalidPlacementTest,
	"Landship.Enemies.Behavior.InvalidGuardPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyInvalidPlacementTest::RunTest(const FString& Parameters)
{
	for (const bool bMissing : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
		ALSEnemyTroop* Troop = SpawnEnemyTestActor<ALSEnemyTroop>(World, FVector(2000, 0, 100));
		Troop->GetCharacterMovement()->DisableMovement();
		if (!bMissing) { Troop->Behavior->GuardedOutpost = Setup->CoreOutpost; }
		ALSBasicAttackTestActor* Opponent = SpawnEnemyTestActor<ALSBasicAttackTestActor>(World, FVector(2100, 0, 100));
		FDataValidationContext Validation;
		TestEqual(TEXT("잘못된 방어 배치는 에디터 오류"), Troop->Behavior->IsDataValid(Validation), EDataValidationResult::Invalid);
		AddExpectedMessage(bMissing ? TEXT("Guard requires an explicit same-world outpost") :
			TEXT("Guard initial placement must be inside its outpost defense radius"), ELogVerbosity::Warning);
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		Setup->CoreOutpost->BasicAttack->Deactivate();
		AdvanceEnemyWorld(World, 0.5f);
		TestEqual(TEXT("잘못된 배치는 명시적 준비 대기"), Troop->Behavior->GetState(), ELSEnemyState::WaitingForSetup);
		TestTrue(TEXT("정지 원인 제공"), Troop->Behavior->GetLastStopReason().Contains(TEXT("Guard")));
		TestNull(TEXT("잘못된 구성은 공격하지 않음"), Troop->BasicAttack->GetAttackTarget());
		TestEqual(TEXT("상대 피해 없음"), EnemyTestHealth(Opponent), 100.0f);
		TestWorld.ForwardErrorMessages(this);
	}
	FDataValidationContext DefaultValidation;
	TestTrue(TEXT("클래스 기본값은 인스턴스 거점 참조를 요구하지 않음"),
		GetDefault<ALSEnemyTroop>()->Behavior->IsDataValid(DefaultValidation) != EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyLateConfigurationTest,
	"Landship.Enemies.Behavior.LateConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyLateConfigurationTest::RunTest(const FString& Parameters)
{
	for (const bool bBeforeFirstDecision : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SetupEnemyTestBattlefield(World);
		ALSEnemyTroop* Troop = SpawnEnemyTestActor<ALSEnemyTroop>(World, FVector(500, 0, 100));
		Troop->GetCharacterMovement()->DisableMovement();
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		if (!bBeforeFirstDecision)
		{
			AddExpectedMessage(TEXT("Guard requires an explicit same-world outpost"), ELogVerbosity::Warning);
			AdvanceEnemyWorld(World, 0.02f);
			TestEqual(TEXT("누락 참조는 명시적으로 대기"), Troop->Behavior->GetState(), ELSEnemyState::WaitingForSetup);
		}
		Troop->Behavior->GuardedOutpost = Setup->CoreOutpost;
		AdvanceEnemyWorld(World, 0.15f);
		TestEqual(TEXT("초기 준비 중 참조 지정 후 방어 시작"), Troop->Behavior->GetState(), ELSEnemyState::GuardIdle);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

#endif
