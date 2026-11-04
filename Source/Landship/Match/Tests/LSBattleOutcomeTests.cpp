#if WITH_DEV_AUTOMATION_TESTS

#include "Match/LSBattleOutcomePolicy.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"
#include "Match/Tests/LSBattleOutcomeTestObserver.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/LSCastleProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/Tests/LSBattlefieldStateTestObserver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "Troops/LSTroopProductionComponent.h"
#include "Troops/LSTroopUnit.h"
#include "TimerManager.h"

namespace
{
template <typename T>
T* SpawnOutcomeActor(UWorld* World, const FVector& Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

void TickOutcomeWorld(UWorld* World, float Delta = 0.01f)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, Delta);
}

float OutcomeHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>();
	return Health && Health->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

float DamageOutcomeActor(AActor* Actor, float Damage)
{
	return Actor->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(Damage, nullptr, nullptr)).AppliedDamage;
}

void ConfigureOutcomeAttack(ULSBasicAttackComponent* Attack, float Damage)
{
	FLSBasicAttackSettings Settings;
	Settings.Damage = Damage;
	Settings.Range = 500;
	Settings.Interval = 0.05f;
	Attack->ConfigureAttack(Settings);
	Attack->ConfigureProjectile(FLSProjectileAttackSettings());
}

void SpawnOutcomeFloor(UWorld* World)
{
	AActor* Floor = SpawnOutcomeActor<AActor>(World, FVector::ZeroVector);
	UBoxComponent* Collision = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Collision);
	Floor->AddInstanceComponent(Collision);
	Collision->SetBoxExtent(FVector(40000, 4000, 25));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(LSCollisionChannels::Ground);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetMobility(EComponentMobility::Static);
	Collision->SetWorldLocation(FVector(0, 0, -25));
	Collision->RegisterComponent();
}

ALSBattlefieldSetup* SpawnOutcomeSetup(UWorld* World, bool bMobile = true)
{
	World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
	ALSBattlefieldSetup* Setup = SpawnOutcomeActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = bMobile
		? static_cast<AActor*>(LSCastleTestFixture::Spawn(World, FVector(10000, 0, 200)))
		: static_cast<AActor*>(SpawnOutcomeActor<ALSBattleOutcomeTestCastle>(World, FVector(10000, 0, 200)));
	Setup->CoreOutpost = SpawnOutcomeActor<ALSEnemyOutpost>(World, FVector(0, 0, 200));
	Setup->CoreOutpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
	return Setup;
}

ALSGameState* BeginOutcomeWorld(FTestWorldWrapper& TestWorld, FAutomationTestBase& Test, bool bEnabled = true)
{
	if (!Test.TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) return nullptr;
	UWorld* World = TestWorld.GetTestWorld();
	if (!Test.TestNotNull(TEXT("실제 권한 GameMode"), World->GetAuthGameMode<ALSGameMode>())) return nullptr;
	ALSGameState* State = World->GetGameState<ALSGameState>();
	if (!Test.TestNotNull(TEXT("실제 GameState"), State)) return nullptr;
	Test.TestEqual(TEXT("맵 설정에 따른 판정 활성"), State->IsBattleOutcomeEnabled(), bEnabled);
	Test.TestEqual(TEXT("시작 결과"), State->GetBattleOutcome(), ELSBattleOutcome::InProgress);
	return State;
}

ULSBattleOutcomeTestObserver* ObserveOutcome(ALSGameState* State)
{
	ULSBattleOutcomeTestObserver* Observer = NewObject<ULSBattleOutcomeTestObserver>(State);
	State->OnBattleOutcomeFinalizedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnFinalized);
	return Observer;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomePolicyTest, "Landship.Match.Outcome.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomePolicyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("사건 없음"), LSBattleOutcome::Evaluate(false, false), ELSBattleOutcome::InProgress);
	TestEqual(TEXT("핵심 함락"), LSBattleOutcome::Evaluate(true, false), ELSBattleOutcome::Victory);
	TestEqual(TEXT("성 사망"), LSBattleOutcome::Evaluate(false, true), ELSBattleOutcome::Defeat);
	TestEqual(TEXT("동일 구간 양쪽 사망"), LSBattleOutcome::Evaluate(true, true), ELSBattleOutcome::MutualDestruction);
	TestFalse(TEXT("진행 중은 승리가 아님"), LSBattleOutcome::IsVictory(ELSBattleOutcome::InProgress));
	TestFalse(TEXT("패배는 승리가 아님"), LSBattleOutcome::IsVictory(ELSBattleOutcome::Defeat));
	TestTrue(TEXT("승리 표시"), LSBattleOutcome::IsVictory(ELSBattleOutcome::Victory));
	TestTrue(TEXT("동시 파괴도 승리 표시"), LSBattleOutcome::IsVictory(ELSBattleOutcome::MutualDestruction));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeDualStrikeTest, "Landship.Match.Outcome.SameFrameActualStrikes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeDualStrikeTest::RunTest(const FString& Parameters)
{
	for (const bool bCoreFirst : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
		ALSCastle* Castle = CastChecked<ALSCastle>(Setup->PlayerCastle);
		Castle->GetMovementComponent()->Deactivate();
		ALSBasicAttackTestActor* Ally = SpawnOutcomeActor<ALSBasicAttackTestActor>(World, FVector(250, 0, 200));
		// 독립 거점의 실제 적 resolver를 사용해 핵심 함락 뒤의 성 타격도 검사한다.
		ALSEnemyOutpost* CastleAttacker = SpawnOutcomeActor<ALSEnemyOutpost>(World, FVector(10350, 0, 200));
		CastleAttacker->BasicAttack->SetAutoAttackOnBeginPlay(false);
		ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
		if (!State) return false;
		Setup->CoreOutpost->FindComponentByClass<ULSEnemyBehaviorComponent>()->SetComponentTickEnabled(false);
		CastleAttacker->Behavior->SetComponentTickEnabled(false);
		ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
		Observer->AttackToInspect = CastleAttacker->BasicAttack;
		Ally->Attack->OnAttackExecutedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnExecuted);
		CastleAttacker->BasicAttack->OnAttackExecutedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnExecuted);
		ConfigureOutcomeAttack(Ally->Attack, 1);
		ConfigureOutcomeAttack(CastleAttacker->BasicAttack, 1);
		// 첫 타이머를 Pending이 아닌 Active로 등록해야 하위 스텝 뒤 만료 시각이 달라진다.
		TickOutcomeWorld(World);
		ULSBasicAttackComponent* First = bCoreFirst ? Ally->Attack.Get() : CastleAttacker->BasicAttack.Get();
		ULSBasicAttackComponent* Second = bCoreFirst ? CastleAttacker->BasicAttack.Get() : Ally->Attack.Get();
		AActor* FirstTarget = bCoreFirst ? static_cast<AActor*>(Setup->CoreOutpost.Get()) : Setup->PlayerCastle.Get();
		AActor* SecondTarget = bCoreFirst ? Setup->PlayerCastle.Get() : static_cast<AActor*>(Setup->CoreOutpost.Get());
		const float InitialCoreHealth = OutcomeHealth(Setup->CoreOutpost);
		const float InitialCastleHealth = OutcomeHealth(Setup->PlayerCastle);
		if (!TestTrue(TEXT("첫 준비 타격"), First->StartAttack(FirstTarget))) return false;
		TickOutcomeWorld(World, 0.005f);
		if (!TestTrue(TEXT("둘째 준비 타격"), Second->StartAttack(SecondTarget))) return false;
		if (!TestEqual(TEXT("양쪽 실제 준비 타격"), Observer->ExecutionSources.Num(), 2)) return false;
		TestEqual(TEXT("실제 적 resolver가 성을 선택"), CastleAttacker->BasicAttack->GetAttackTarget(), Setup->PlayerCastle.Get());
		TestEqual(TEXT("준비 타격이 실제 핵심 피해 적용"), OutcomeHealth(Setup->CoreOutpost), InitialCoreHealth - 1);
		TestEqual(TEXT("준비 타격이 실제 성 피해 적용"), OutcomeHealth(Setup->PlayerCastle), InitialCastleHealth - 1);
		DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost) - 10);
		// 출처 없는 준비 피해는 거점의 피격 지원 요청을 만들지 않는다.
		DamageOutcomeActor(Setup->PlayerCastle, OutcomeHealth(Setup->PlayerCastle) - 10);
		TestEqual(TEXT("핵심 잔여 체력"), OutcomeHealth(Setup->CoreOutpost), 10.0f);
		TestEqual(TEXT("성 잔여 체력"), OutcomeHealth(Setup->PlayerCastle), 10.0f);
		ConfigureOutcomeAttack(Ally->Attack, 10);
		ConfigureOutcomeAttack(CastleAttacker->BasicAttack, 10);
		Observer->ExecutionSources.Reset();
		Observer->ExecutionFrames.Reset();
		TickOutcomeWorld(World, 0.1f);
		TestTrue(TEXT("실제 핵심 함락"), Setup->CoreOutpost->HasBeenCaptured());
		TestTrue(TEXT("실제 성 사망 보존"), World->GetSubsystem<ULSBattlefieldStateSubsystem>()->HasObservedPlayerCastleDeath());
		TestEqual(TEXT("동일 프레임 내부 분류"), State->GetBattleOutcome(), ELSBattleOutcome::MutualDestruction);
		if (TestEqual(TEXT("같은 월드 Tick에서 두 치명타"), Observer->ExecutionSources.Num(), 2))
		{
			TestTrue(TEXT("만료 시각이 빠른 타격이 먼저 실행"), Observer->ExecutionSources[0].Get() == First->GetOwner());
			TestTrue(TEXT("나중 타격도 실행"), Observer->ExecutionSources[1].Get() == Second->GetOwner());
			TestEqual(TEXT("두 타격의 프레임 일치"), Observer->ExecutionFrames[0], Observer->ExecutionFrames[1]);
			TestEqual(TEXT("같은 프레임 말미 확정"), Observer->FinalizedFrame, Observer->ExecutionFrames[0]);
		}
		TestEqual(TEXT("확정 알림 한 번"), Observer->Outcomes.Num(), 1);
		TestTrue(TEXT("알림 전에 실행 정지"), Observer->bSawStoppedAttackAtFinalization);
		TestEqual(TEXT("생존 적 실행 상태 종료"), CastleAttacker->Behavior->GetState(), ELSEnemyState::Ended);
		TickOutcomeWorld(World, 0.1f);
		TestEqual(TEXT("추가 수집으로 중복 알림 없음"), Observer->Outcomes.Num(), 1);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeCastleLifetimeTest, "Landship.Match.Outcome.CastleRepresentationsAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeCastleLifetimeTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 5; ++Scenario)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World, Scenario == 0);
		ALSBattleOutcomeTestCastle* Castle = Cast<ALSBattleOutcomeTestCastle>(Setup->PlayerCastle);
		if (Castle)
		{
			Castle->bDestroyDuringDeath = Scenario == 2;
			Castle->bLeaveHealthUnready = Scenario == 4;
		}
		ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
		if (!State) return false;
		ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
		Setup->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->OnDiedEvent.AddDynamic(
			Observer, &ULSBattleOutcomeTestObserver::OnCastleDied);
		if (Scenario <= 2)
		{
			TestTrue(TEXT("실제 성 치명 피해"), DamageOutcomeActor(Setup->PlayerCastle, OutcomeHealth(Setup->PlayerCastle)) > 0);
			TestFalse(TEXT("수집 전에는 아직 미확정"), State->HasBattleEnded());
			TickOutcomeWorld(World);
			TestEqual(TEXT("두 성 표현과 필수 훅 중 제거 모두 패배"), State->GetBattleOutcome(), ELSBattleOutcome::Defeat);
			TestEqual(TEXT("확정 한 번"), Observer->Outcomes.Num(), 1);
			if (Scenario == 2)
			{
				TestTrue(TEXT("필수 훅에서 성 제거"), Castle->IsActorBeingDestroyed());
				TestEqual(TEXT("공개 사망 알림은 생략됨"), Observer->PublicCastleDeaths, 0);
			}
		}
		else
		{
			if (Scenario == 3) Setup->PlayerCastle->Destroy();
			if (Scenario == 4)
			{
				const FString Prefix = TEXT("[LSHealthComponent::ApplyDamage] Health not ready");
				AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 1);
				FEnsureScope EnsureScope([&](const FEnsureHandlerArgs& Args)
				{
					if (!FString(Args.Message).StartsWith(Prefix)) return false;
					AddError(Args.Message);
					return true;
				});
				const FLSApplyDamageResult Rejected = Setup->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
					ULSHealthComponent::MakeDamageRequest(100, nullptr, nullptr));
				TestEqual(TEXT("미준비 성의 치명 피해 요청 거부"), Rejected.Status, ELSApplyDamageStatus::RejectedNotReady);
				TestEqual(TEXT("미준비 성에 적용한 피해 없음"), Rejected.AppliedDamage, 0.0f);
				TestEqual(TEXT("미준비 진단 한 번"), EnsureScope.GetCount(), 1);
			}
			TickOutcomeWorld(World);
			TestEqual(TEXT("비사망 제거와 미준비 체력은 미종료"), State->GetBattleOutcome(), ELSBattleOutcome::InProgress);
			TestTrue(TEXT("종료 알림 없음"), Observer->Outcomes.IsEmpty());
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeNonterminalTest, "Landship.Match.Outcome.NonterminalAndInvalidSetup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeNonterminalTest::RunTest(const FString& Parameters)
{
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
		ALSBattlefieldSetup* Setup = Scenario == 0 ? SpawnOutcomeSetup(World) : nullptr;
		if (Scenario == 2) SpawnOutcomeActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
		ALSTroopUnit* Troop = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(20000, 0, 200));
		ALSEnemyOutpost* Ordinary = SpawnOutcomeActor<ALSEnemyOutpost>(World, FVector(30000, 0, 200));
		Ordinary->BasicAttack->SetAutoAttackOnBeginPlay(false);
		ALSGameState* State = BeginOutcomeWorld(TestWorld, *this, Scenario == 0);
		if (!State) return false;
		ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
		TestTrue(TEXT("병력 전멸을 위한 실제 사망"), DamageOutcomeActor(Troop, OutcomeHealth(Troop)) > 0);
		TestTrue(TEXT("일반 거점 실제 치명 피해"), DamageOutcomeActor(Ordinary, OutcomeHealth(Ordinary)) > 0);
		TickOutcomeWorld(World);
		TestTrue(TEXT("일반 거점은 함락"), Ordinary->HasBeenCaptured());
		TestFalse(TEXT("병력 전멸과 일반 거점은 미종료"), State->HasBattleEnded());
		TestTrue(TEXT("종료 알림 없음"), Observer->Outcomes.IsEmpty());
		if (Setup) TestFalse(TEXT("핵심은 살아 있음"), Setup->CoreOutpost->HasBeenCaptured());
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeFirstResultTest, "Landship.Match.Outcome.FirstResultRemainsFinal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeFirstResultTest::RunTest(const FString& Parameters)
{
	for (const bool bVictoryFirst : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
		ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
		if (!State) return false;
		ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
		AActor* First = bVictoryFirst ? static_cast<AActor*>(Setup->CoreOutpost.Get()) : Setup->PlayerCastle.Get();
		AActor* Later = bVictoryFirst ? Setup->PlayerCastle.Get() : static_cast<AActor*>(Setup->CoreOutpost.Get());
		DamageOutcomeActor(First, OutcomeHealth(First));
		TickOutcomeWorld(World);
		const ELSBattleOutcome Expected = bVictoryFirst ? ELSBattleOutcome::Victory : ELSBattleOutcome::Defeat;
		TestEqual(TEXT("첫 프레임 결과"), State->GetBattleOutcome(), Expected);
		DamageOutcomeActor(Later, OutcomeHealth(Later));
		TickOutcomeWorld(World);
		TickOutcomeWorld(World);
		TestTrue(TEXT("늦은 핵심 함락 사실도 보존"), Setup->CoreOutpost->HasBeenCaptured());
		TestTrue(TEXT("늦은 성 사망 사실도 보존"), World->GetSubsystem<ULSBattlefieldStateSubsystem>()->HasObservedPlayerCastleDeath());
		TestEqual(TEXT("서로 다른 프레임은 동시 파괴로 바뀌지 않음"), State->GetBattleOutcome(), Expected);
		TestEqual(TEXT("늦은 사실과 추가 수집에도 확정 알림 한 번"), Observer->Outcomes.Num(), 1);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeRemainingProjectileTest, "Landship.Match.Outcome.RemainingProjectileAfterDefeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeRemainingProjectileTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
	ALSTroopUnit* Source = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(500, 0, 200));
	Source->GetCharacterMovement()->DisableMovement();
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
	ULSBattlefieldStateSubsystem* Battlefield = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	ULSBattlefieldStateTestObserver* CaptureObserver = NewObject<ULSBattlefieldStateTestObserver>(Battlefield);
	Battlefield->OnCoreOutpostCapturedEvent.AddDynamic(CaptureObserver, &ULSBattlefieldStateTestObserver::OnCaptured);
	Setup->CoreOutpost->BasicAttack->StopAttack();
	Setup->CoreOutpost->FindComponentByClass<ULSEnemyBehaviorComponent>()->SetComponentTickEnabled(false);
	FLSBasicAttackSettings Attack;
	Attack.Damage = OutcomeHealth(Setup->CoreOutpost);
	Attack.Range = 1000;
	Attack.Interval = 1;
	Source->BasicAttack->ConfigureAttack(Attack);
	FLSProjectileAttackSettings Projectile;
	Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
	Projectile.Speed = 1000;
	Projectile.Lifetime = 2;
	Source->BasicAttack->ConfigureProjectile(Projectile);
	if (!TestTrue(TEXT("결과 전에 실제 발사"), Source->BasicAttack->StartAttack(Setup->CoreOutpost))) return false;
	TWeakObjectPtr<ALSBasicProjectile> Shot;
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It) Shot = *It;
	if (!TestTrue(TEXT("실제 비행 중 투사체"), Shot.IsValid())) return false;
	DamageOutcomeActor(Setup->PlayerCastle, OutcomeHealth(Setup->PlayerCastle));
	TickOutcomeWorld(World);
	TestEqual(TEXT("명중 전 패배 확정"), State->GetBattleOutcome(), ELSBattleOutcome::Defeat);
	TestFalse(TEXT("핵심은 아직 생존"), Setup->CoreOutpost->HasBeenCaptured());
	TestTrue(TEXT("생존 핵심 query collision 유지"), Setup->CoreOutpost->GetActorEnableCollision());
	Source->Destroy();
	for (int32 Index = 0; Index < 80; ++Index) TickOutcomeWorld(World);
	TestTrue(TEXT("발사자 제거 뒤 잔여 투사체가 핵심 함락"), Setup->CoreOutpost->HasBeenCaptured());
	TestFalse(TEXT("명중 후 투사체 제거"), Shot.IsValid());
	TestEqual(TEXT("패배 뒤에도 원본 함락 알림"), CaptureObserver->Captures.Num(), 1);
	TestEqual(TEXT("잔여 투사체의 늦은 함락은 패배 유지"), State->GetBattleOutcome(), ELSBattleOutcome::Defeat);
	TestEqual(TEXT("결과 알림은 여전히 한 번"), Observer->Outcomes.Num(), 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeStopsActionsTest, "Landship.Match.Outcome.StopsActionsAndRejectsRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeStopsActionsTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World, false);
	ALSTroopUnit* Troop = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(20000, 0, 200));
	ALSBasicAttackTestActor* Target = SpawnOutcomeActor<ALSBasicAttackTestActor>(World, FVector(20300, 0, 200));
	Target->SetInitialFaction(ELSFaction::Enemy);
	ALSCastle* Mobile = LSCastleTestFixture::Spawn(World, FVector(25000, 0, 200));
	ULSTroopProductionComponent* Production = NewObject<ULSTroopProductionComponent>(Setup);
	Setup->AddInstanceComponent(Production);
	Production->RegisterComponent();
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	Troop->GetCharacterMovement()->DisableMovement();
	Mobile->GetMovementComponent()->Deactivate();
	ConfigureOutcomeAttack(Troop->BasicAttack, 1);
	FLSOrder AttackOrder;
	AttackOrder.Kind = ELSOrderKind::AttackTarget;
	AttackOrder.Target = Target;
	FLSOrder HoldOrder;
	HoldOrder.Kind = ELSOrderKind::Hold;
	if (!TestTrue(TEXT("진행 중 실제 공격 명령"), Troop->Orders->SubmitOrder(AttackOrder))) return false;
	TestTrue(TEXT("진행 중 명령 예약"), Troop->Orders->SubmitOrder(HoldOrder, true));
	TestEqual(TEXT("예약이 실제 존재"), Troop->Orders->GetQueuedCount(), 1);
	TestNotNull(TEXT("진행 중 공격 대상"), Troop->BasicAttack->GetAttackTarget());
	Troop->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Troop->GetCharacterMovement()->Velocity = FVector(10, 0, 0);
	Mobile->GetMovementComponent()->Activate();
	Mobile->GetMovementComponent()->Velocity = FVector(10, 0, 0);
	ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
	Observer->AttackToInspect = Troop->BasicAttack;
	DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost));
	TickOutcomeWorld(World);
	TestTrue(TEXT("알림 전에 공격 정리"), Observer->bSawStoppedAttackAtFinalization);
	TestEqual(TEXT("예약 정리"), Troop->Orders->GetQueuedCount(), 0);
	TestNull(TEXT("교전 대상 정리"), Troop->Orders->GetEngagedTarget());
	TestEqual(TEXT("병력 이동 차단"), Troop->GetCharacterMovement()->MovementMode, MOVE_None);
	TestEqual(TEXT("이동형 성 이동 차단"), Mobile->GetMovementComponent()->IsActive(), false);
	TestTrue(TEXT("병력 속도 정리"), Troop->GetVelocity().IsNearlyZero());
	TestTrue(TEXT("이동형 성 속도 정리"), Mobile->GetVelocity().IsNearlyZero());
	TestFalse(TEXT("성 직접 이동 재요청 거부"), CastChecked<ALSCastle>(Setup->PlayerCastle)->RequestMove(FVector(11000, 0, 200)));
	TestFalse(TEXT("종료 뒤 예약 명령 거부"), Troop->Orders->SubmitOrder(HoldOrder, true));
	TestFalse(TEXT("종료 뒤 직접 공격 거부"), Troop->BasicAttack->StartAttack(Target));
	TestFalse(TEXT("종료 뒤 직접 자동 공격 거부"), Troop->BasicAttack->StartAutoAttack());
	Troop->BasicAttack->Deactivate();
	Troop->BasicAttack->Activate(true);
	Troop->Restart();
	Mobile->Restart();
	TestFalse(TEXT("컴포넌트 재활성화 뒤 공격 거부"), Troop->BasicAttack->StartAttack(Target));
	TestEqual(TEXT("Restart 뒤 병력 정지 유지"), Troop->GetCharacterMovement()->MovementMode, MOVE_None);
	TestEqual(TEXT("Restart 뒤 이동형 성 정지 유지"), Mobile->GetMovementComponent()->IsActive(), false);
	TestEqual(TEXT("종료 뒤 생산 상태"), Production->GetStatus(ELSTroopType::Spearman), ELSProductionResult::Unavailable);
	TestEqual(TEXT("종료 뒤 직접 생산 거부"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Unavailable);
	TestEqual(TEXT("종료 뒤 수동 무장 상태"), Mobile->Weapons->GetStatus(ELSCastleWeapon::Cannon), ELSWeaponCastResult::Unavailable);
	TestEqual(TEXT("종료 뒤 수동 무장 직접 사용 거부"), Mobile->Weapons->CastWeapon(ELSCastleWeapon::Cannon, Target->GetActorLocation()), ELSWeaponCastResult::Unavailable);
	TestEqual(TEXT("종료 뒤 무장 비활성 설정 허용"), Mobile->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false), ELSWeaponChangeResult::Success);
	TestEqual(TEXT("종료 뒤 무장 활성 설정 허용"), Mobile->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, true), ELSWeaponChangeResult::Success);
	TestEqual(TEXT("종료 뒤 무장 성장 허용"), Mobile->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon), ELSWeaponChangeResult::Success);
	FLSCastleWeaponState WeaponState;
	TestTrue(TEXT("종료 뒤 변경 상태 조회"), Mobile->Weapons->GetWeaponState(ELSCastleWeapon::Cannon, WeaponState));
	TestEqual(TEXT("실제 성장 레벨"), WeaponState.Level, 2);
	TestTrue(TEXT("실제 활성 설정 반영"), WeaponState.bEnabled);
	TestEqual(TEXT("설정 변경 뒤에도 발사는 차단"), Mobile->Weapons->GetStatus(ELSCastleWeapon::Cannon), ELSWeaponCastResult::Unavailable);
	const float HealthAfterEnd = OutcomeHealth(Target);
	for (int32 Index = 0; Index < 20; ++Index) TickOutcomeWorld(World);
	TestEqual(TEXT("남은 타이머가 새 피해를 만들지 않음"), OutcomeHealth(Target), HealthAfterEnd);
	ALSTroopUnit* LateTroop = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(30000, 0, 200));
	TestEqual(TEXT("늦게 생성한 병력도 정지"), LateTroop->GetCharacterMovement()->MovementMode, MOVE_None);
	TestFalse(TEXT("늦게 생성한 병력의 직접 공격 거부"), LateTroop->BasicAttack->StartAutoAttack());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeAttackTerminalTest, "Landship.Match.Outcome.BasicAttackTerminalBeforeBeginPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeAttackTerminalTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnOutcomeActor<ALSBasicAttackTestActor>(World, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Target = SpawnOutcomeActor<ALSBasicAttackTestActor>(World, FVector(100, 0, 100));
	Target->SetInitialFaction(ELSFaction::Enemy);
	ConfigureOutcomeAttack(Source->Attack, 10);
	Source->Attack->SetAutoAttackOnBeginPlay(true);
	Source->Attack->EndBattleActions();
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	Source->Attack->Activate(true);
	TestFalse(TEXT("BeginPlay 전 종료된 공격은 직접 시작 거부"), Source->Attack->StartAttack(Target));
	TestFalse(TEXT("BeginPlay 전 종료된 공격은 자동 시작 거부"), Source->Attack->StartAutoAttack());
	for (int32 Index = 0; Index < 20; ++Index) TickOutcomeWorld(World);
	TestEqual(TEXT("BeginPlay 자동 시작도 새 피해를 만들지 않음"), Target->GetCurrentHealth(), 100.0f);
	ALSBasicAttackTestActor* Restartable = SpawnOutcomeActor<ALSBasicAttackTestActor>(World, FVector(100, 200, 100));
	ConfigureOutcomeAttack(Restartable->Attack, 1);
	TestTrue(TEXT("일반 공격 시작"), Restartable->Attack->StartAttack(Target));
	Restartable->Attack->StopAttack();
	TestTrue(TEXT("일반 StopAttack은 재시작 가능"), Restartable->Attack->StartAttack(Target));
	Restartable->Attack->EndBattleActions();
	TestFalse(TEXT("영구 종료는 재시작 거부"), Restartable->Attack->StartAttack(Target));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeInputSeparationTest, "Landship.Match.Outcome.CombatInputAndCameraSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeInputSeparationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
	ALSPlayerController* Controller = SpawnOutcomeActor<ALSPlayerController>(World, FVector::ZeroVector);
	ALSRTSCameraPawn* Camera = SpawnOutcomeActor<ALSRTSCameraPawn>(World, FVector::ZeroVector);
	ALSTroopUnit* Troop = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(20000, 0, 200));
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	Controller->Possess(Camera);
	Camera->SetBattlefieldBounds(FBox2D(FVector2D(-40000, -40000), FVector2D(40000, 40000)), 0);
	Controller->ApplySelection({Troop});
	Controller->UseControlGroup(1, true);
	Controller->SetPendingInput(ELSCommandInput::Attack);
	TestEqual(TEXT("종료 전 입력 준비"), Controller->GetPendingInput(), ELSCommandInput::Attack);
	DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost));
	TickOutcomeWorld(World);
	TestEqual(TEXT("종료 시 준비 입력 취소"), Controller->GetPendingInput(), ELSCommandInput::None);
	TestTrue(TEXT("살아 있는 기존 선택 유지"), Controller->GetSelectedActors().Contains(Troop));
	Controller->ApplySelection({Setup->PlayerCastle.Get()});
	TestTrue(TEXT("종료 뒤 선택 교체 거부"), Controller->GetSelectedActors().Contains(Troop));
	Controller->SetPendingInput(ELSCommandInput::Move);
	TestEqual(TEXT("공개 입력 준비 함수도 차단"), Controller->GetPendingInput(), ELSCommandInput::None);
	TestFalse(TEXT("공개 명령 전달 차단"), Controller->IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false));
	TestTrue(TEXT("종료 뒤 미니맵 좌클릭도 소비"), Controller->HandleMinimapInput(FVector(100, 100, 0), false, false));
	TestTrue(TEXT("미니맵 우클릭은 전투 명령으로 소비"), Controller->HandleMinimapInput(FVector(100, 100, 0), true, false));
	TestFalse(TEXT("무장 준비 차단"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->ProduceTroop(ELSTroopType::Spearman);
	TestEqual(TEXT("컨트롤러 생산 차단"), Controller->GetLastProductionResult(), ELSProductionResult::Unavailable);
	const float BeforeZoom = Camera->GetZoomDistance();
	const FVector BeforeCamera = Camera->GetActorLocation();
	Camera->Zoom(1);
	TestEqual(TEXT("종료 뒤 카메라 확대 차단"), Camera->GetZoomDistance(), BeforeZoom);
	Camera->Pan(FVector2D(1, 1), 1);
	Camera->Drag(FVector2D(100, 100));
	Camera->FocusLocation(Troop->GetActorLocation());
	Controller->FocusSelection();
	TestTrue(TEXT("종료 뒤 이동·드래그·직접 초점·Home 모두 차단"), Camera->GetActorLocation().Equals(BeforeCamera));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeWorldIsolationTest, "Landship.Match.Outcome.ManagedWorldIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeWorldIsolationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper FirstWorld;
	FTestWorldWrapper SecondWorld;
	if (!FirstWorld.CreateTestWorld(EWorldType::Game) || !SecondWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* First = FirstWorld.GetTestWorld();
	UWorld* Second = SecondWorld.GetTestWorld();
	ALSBattlefieldSetup* FirstSetup = SpawnOutcomeSetup(First);
	ALSBattlefieldSetup* SecondSetup = SpawnOutcomeSetup(Second);
	ALSGameState* FirstState = BeginOutcomeWorld(FirstWorld, *this);
	ALSGameState* SecondState = BeginOutcomeWorld(SecondWorld, *this);
	if (!FirstState || !SecondState) return false;
	ULSBattleOutcomeTestObserver* FirstObserver = ObserveOutcome(FirstState);
	ULSBattleOutcomeTestObserver* SecondObserver = ObserveOutcome(SecondState);
	DamageOutcomeActor(FirstSetup->CoreOutpost, OutcomeHealth(FirstSetup->CoreOutpost));
	TickOutcomeWorld(Second);
	TestFalse(TEXT("다른 월드의 Tick은 첫 월드 결과를 수집하지 않음"), FirstState->HasBattleEnded());
	TestFalse(TEXT("두 번째 월드도 독립적으로 진행 중"), SecondState->HasBattleEnded());
	TickOutcomeWorld(First);
	TestEqual(TEXT("자기 월드 Tick에서만 승리 확정"), FirstState->GetBattleOutcome(), ELSBattleOutcome::Victory);
	TestTrue(TEXT("첫 월드 확정이 다른 월드에 알림을 보내지 않음"), SecondObserver->Outcomes.IsEmpty());
	DamageOutcomeActor(SecondSetup->PlayerCastle, OutcomeHealth(SecondSetup->PlayerCastle));
	TickOutcomeWorld(First);
	TestFalse(TEXT("첫 월드 재수집은 두 번째 월드 패배를 확정하지 않음"), SecondState->HasBattleEnded());
	TickOutcomeWorld(Second);
	TestEqual(TEXT("두 번째 월드의 독립 패배"), SecondState->GetBattleOutcome(), ELSBattleOutcome::Defeat);
	TestEqual(TEXT("첫 월드 결과 유지"), FirstState->GetBattleOutcome(), ELSBattleOutcome::Victory);
	TestEqual(TEXT("첫 월드 알림 한 번"), FirstObserver->Outcomes.Num(), 1);
	TestEqual(TEXT("두 번째 월드 알림 한 번"), SecondObserver->Outcomes.Num(), 1);
	FirstWorld.ForwardErrorMessages(this);
	SecondWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeOutsideTickIntervalTest, "Landship.Match.Outcome.OutsideTickAndTimerShareCollectionInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeOutsideTickIntervalTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	ULSBattleOutcomeTestObserver* Observer = ObserveOutcome(State);
	TickOutcomeWorld(World);
	DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost));
	TestFalse(TEXT("Tick 밖 함락은 다음 수집까지 미확정"), State->HasBattleEnded());
	FTimerHandle CastleDeathTimer;
	bool bTimerExecuted = false;
	World->GetTimerManager().SetTimer(CastleDeathTimer, FTimerDelegate::CreateLambda([&]
	{
		bTimerExecuted = true;
		DamageOutcomeActor(Setup->PlayerCastle, OutcomeHealth(Setup->PlayerCastle));
	}), 0.005f, false);
	TickOutcomeWorld(World, 0.01f);
	TestTrue(TEXT("다음 월드 Tick 안에서 성 사망 타이머 실행"), bTimerExecuted);
	TestEqual(TEXT("수집을 사이에 두지 않은 사건은 동시 파괴"), State->GetBattleOutcome(), ELSBattleOutcome::MutualDestruction);
	TestEqual(TEXT("같은 수집 구간의 알림 한 번"), Observer->Outcomes.Num(), 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeFallingTest, "Landship.Match.Outcome.FallingContinuesAndStopsOnLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeFallingTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
	SpawnOutcomeFloor(World);
	ALSTroopUnit* Falling = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(20000, 0, 700));
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	Falling->Orders->SubmitOrder(Hold);
	Falling->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Falling->GetCharacterMovement()->Velocity = FVector(80, 0, -100);
	const FVector Start = Falling->GetActorLocation();
	DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost));
	TickOutcomeWorld(World);
	TestEqual(TEXT("낙하 중 승리 확정"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
	TestTrue(TEXT("확정 뒤 낙하 모드 유지"), Falling->GetCharacterMovement()->IsFalling());
	const FVector VelocityBeforeRestart = Falling->GetVelocity();
	Falling->Restart();
	TestTrue(TEXT("Restart 뒤 낙하 궤적 유지"), Falling->GetCharacterMovement()->IsFalling() && Falling->GetVelocity().Equals(VelocityBeforeRestart, 0.01));
	for (int32 Index = 0; Index < 300 && Falling->GetCharacterMovement()->MovementMode != MOVE_None; ++Index)
		TickOutcomeWorld(World);
	const FVector Landed = Falling->GetActorLocation();
	TestTrue(TEXT("확정 뒤 실제로 아래 바닥까지 낙하"), Landed.Z < Start.Z - 400);
	TestTrue(TEXT("기존 수평 속도에 따라 실제 위치 이동"), Landed.X > Start.X + 10);
	TestTrue(TEXT("바닥 위 캡슐 높이에서 착지"), FMath::Abs(Landed.Z - Falling->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) < 5);
	TestEqual(TEXT("착지 뒤 자발 이동 차단"), Falling->GetCharacterMovement()->MovementMode, MOVE_None);
	TestTrue(TEXT("착지 뒤 속도 정리"), Falling->GetVelocity().IsNearlyZero());
	TestFalse(TEXT("착지 뒤 명령 재개 거부"), Falling->Orders->SubmitOrder(Hold));
	for (int32 Index = 0; Index < 20; ++Index) TickOutcomeWorld(World);
	TestTrue(TEXT("착지 뒤 위치 유지"), Falling->GetActorLocation().Equals(Landed, 0.1));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattleOutcomeShotgunKnockbackTest, "Landship.Match.Outcome.RemainingShotgunKnockbackAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattleOutcomeShotgunKnockbackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = SpawnOutcomeSetup(World);
	SpawnOutcomeFloor(World);
	ALSTroopUnit* Enemy = SpawnOutcomeActor<ALSTroopUnit>(World, FVector(20000, 0, 100));
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	ALSGameState* State = BeginOutcomeWorld(TestWorld, *this);
	if (!State) return false;
	const float RestHeight = Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2;
	Enemy->SetActorLocation(FVector(20000, 0, RestHeight));
	Enemy->GetCharacterMovement()->DisableMovement();
	const FVector Start = Enemy->GetActorLocation();
	const float InitialHealth = OutcomeHealth(Enemy);
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 5;
	Settings.PelletCount = 1;
	Settings.Range = 1000;
	Settings.ProjectileSpeed = 1000;
	Settings.KnockbackSpeed = 350;
	Settings.KnockbackLift = 350;
	const FTransform Transform(Start - FVector(600, 0, 0));
	ALSCastleProjectile* Volley = World->SpawnActorDeferred<ALSCastleProjectile>(
		ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestTrue(TEXT("결과 전에 실제 산탄 발사"), Volley->Launch(ELSCastleWeapon::Shotgun, Settings,
		ULSHealthComponent::MakeDamageRequest(Settings.Damage, Setup->PlayerCastle, Setup->PlayerCastle), Start))) return false;
	Volley->FinishSpawning(Transform);
	DamageOutcomeActor(Setup->CoreOutpost, OutcomeHealth(Setup->CoreOutpost));
	TickOutcomeWorld(World);
	TestEqual(TEXT("산탄 명중 전에 승리 확정"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
	TestEqual(TEXT("확정 시 적은 아직 피해 없음"), OutcomeHealth(Enemy), InitialHealth);
	for (int32 Index = 0; Index < 100 && OutcomeHealth(Enemy) == InitialHealth; ++Index) TickOutcomeWorld(World);
	if (!TestEqual(TEXT("종료 뒤 실제 산탄 명중 피해"), OutcomeHealth(Enemy), InitialHealth - Settings.Damage)) return false;
	TestTrue(TEXT("명중 뒤 넉백 상태"), Enemy->Orders->IsKnockedBack());
	TestTrue(TEXT("명중 뒤 보류 Launch 또는 낙하 속도 존재"), !Enemy->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero() || !Enemy->GetVelocity().IsNearlyZero());
	Enemy->Restart();
	float HighestZ = Enemy->GetActorLocation().Z;
	for (int32 Index = 0; Index < 300 && Enemy->GetCharacterMovement()->MovementMode != MOVE_None; ++Index)
	{
		TickOutcomeWorld(World);
		HighestZ = FMath::Max(HighestZ, static_cast<float>(Enemy->GetActorLocation().Z));
	}
	const FVector Landed = Enemy->GetActorLocation();
	TestTrue(TEXT("종료 뒤 산탄이 실제 수평 넉백 발생"), Landed.X > Start.X + 50);
	TestTrue(TEXT("종료 뒤 산탄이 실제 상승 궤적 발생"), HighestZ > Start.Z + 10);
	TestTrue(TEXT("넉백 뒤 실제 바닥 착지"), FMath::Abs(Landed.Z - Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) < 5);
	TestEqual(TEXT("넉백 착지 뒤 이동 차단"), Enemy->GetCharacterMovement()->MovementMode, MOVE_None);
	TestFalse(TEXT("넉백 착지 뒤 공격 재시작 거부"), Enemy->BasicAttack->StartAutoAttack());
	TestEqual(TEXT("넉백 착지 뒤 명령 예약 없음"), Enemy->Orders->GetQueuedCount(), 0);
	for (int32 Index = 0; Index < 20; ++Index) TickOutcomeWorld(World);
	TestTrue(TEXT("넉백 착지 뒤 위치 유지"), Enemy->GetActorLocation().Equals(Landed, 0.1));
	TestEqual(TEXT("잔여 산탄 뒤 결과 유지"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
