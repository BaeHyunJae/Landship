#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/Tests/LSRejectedSupportProjectile.h"
#include "EngineUtils.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

#include "Enemies/Tests/LSEnemySupportTestAccess.h"

namespace
{
template <typename T>
T* SpawnSupportActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Parameters);
}

void AdvanceSupportWorld(UWorld* World, float Duration)
{
	while (Duration > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Duration, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Duration -= Step;
	}
}

void FreezeSupportActor(AActor* Actor)
{
	if (ULSEnemyBehaviorComponent* Behavior = Actor->FindComponentByClass<ULSEnemyBehaviorComponent>())
	{
		Behavior->SetComponentTickEnabled(false);
	}
	if (ULSBasicAttackComponent* Attack = Actor->FindComponentByClass<ULSBasicAttackComponent>()) Attack->StopAttack();
	if (ACharacter* Character = Cast<ACharacter>(Actor)) { Character->GetCharacterMovement()->DisableMovement(); }
	else if (APawn* Pawn = Cast<APawn>(Actor))
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
			Movement->Deactivate();
		}
}

ALSBattlefieldSetup* CreateSupportBattlefield(UWorld* World)
{
	ALSBattlefieldSetup* Setup = SpawnSupportActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(10000, 0, 100));

	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = SpawnSupportActor<ALSEnemyOutpost>(World, FVector(0, -1000, 100));
	Setup->CoreOutpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
	return Setup;
}

ALSEnemyTroop* CreateSupportGuard(UWorld* World, ALSOutpost* Outpost, FVector Location, float Radius)
{
	ALSEnemyTroop* Troop = SpawnSupportActor<ALSEnemyTroop>(World, Location);
	Troop->Behavior->GuardedOutpost = Outpost;
	Troop->Behavior->SupportAcceptanceRadius = Radius;
	Troop->BasicAttack->SetAutoAttackOnBeginPlay(false);
	return Troop;
}

float ApplySupportDamage(AActor* Victim, AActor* Source, float Amount)
{
	return Victim->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(Amount, Source, Source)).AppliedDamage;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemySupportDeliveryTest,
	"Landship.Enemies.Support.DeliveryMergeExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemySupportDeliveryTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = CreateSupportBattlefield(World);
	ALSEnemyOutpost* Reporter = SpawnSupportActor<ALSEnemyOutpost>(World, FVector(0, 0, 100));
	Reporter->BasicAttack->SetAutoAttackOnBeginPlay(false);
	ALSEnemyTroop* Assigned = CreateSupportGuard(World, Reporter, FVector(900, 0, 100), 10);
	ALSEnemyTroop* Near = CreateSupportGuard(World, Setup->CoreOutpost, FVector(100, -500, 100), 600);
	ALSEnemyTroop* Outside = CreateSupportGuard(World, Setup->CoreOutpost, FVector(700, -500, 100), 600);
	// Near의 수신을 재전파하면 이 병력까지 닿으므로 수신 에코를 별도로 검출한다.
	ALSEnemyTroop* EchoOnly = CreateSupportGuard(World, Setup->CoreOutpost, FVector(100, -1000, 100), 600);
	ALSBasicAttackTestActor* Opponent = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(2000, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	for (AActor* Actor : {static_cast<AActor*>(Setup->PlayerCastle), static_cast<AActor*>(Setup->CoreOutpost.Get()),
		static_cast<AActor*>(Reporter), static_cast<AActor*>(Assigned), static_cast<AActor*>(Near),
		static_cast<AActor*>(Outside), static_cast<AActor*>(EchoOnly)}) { FreezeSupportActor(Actor); }
	TestEqual(TEXT("실제 비치명 피해"), ApplySupportDamage(Reporter, Opponent, 1), 1.0f);
	TestEqual(TEXT("전담 수비병은 수신 반경 밖에서도 받음"), FLSEnemySupportTestAccess::Pending(Assigned->Behavior), 1);
	TestEqual(TEXT("일반 병력은 자신의 수신 반경 안에서 받음"), FLSEnemySupportTestAccess::Pending(Near->Behavior), 1);
	TestEqual(TEXT("일반 병력은 수신 반경 밖에서 받지 않음"), FLSEnemySupportTestAccess::Pending(Outside->Behavior), 0);
	TestEqual(TEXT("수신자는 요청을 재전파하지 않음"), FLSEnemySupportTestAccess::Pending(EchoOnly->Behavior), 0);
	if (FLSEnemySupportTestAccess::Pending(Assigned->Behavior) != 1) return false;
	TestEqual(TEXT("지원 상대는 실제 피해 원인 액터"), FLSEnemySupportTestAccess::Opponent(Assigned->Behavior), static_cast<AActor*>(Opponent));
	const uint64 FirstRequest = FLSEnemySupportTestAccess::RequestId(Assigned->Behavior);
	Assigned->Behavior->PendingSupportLifetime = 0.1f;
	Assigned->Behavior->DecisionInterval = 0.01f;
	AdvanceSupportWorld(World, 0.08f);
	const FVector UpdatedLocation(2100, 50, 100);
	Opponent->SetActorLocation(UpdatedLocation);
	ApplySupportDamage(Reporter, Opponent, 1);
	TestEqual(TEXT("같은 요청자·상대의 보고 병합"), FLSEnemySupportTestAccess::Pending(Assigned->Behavior), 1);
	TestTrue(TEXT("새 실제 보고 식별자 반영"), FLSEnemySupportTestAccess::RequestId(Assigned->Behavior) > FirstRequest);
	TestEqual(TEXT("보고 당시 상대 위치 갱신"), FLSEnemySupportTestAccess::ReportedLocation(Assigned->Behavior), UpdatedLocation);
	AdvanceSupportWorld(World, 0.06f);
	Assigned->Behavior->SetComponentTickEnabled(true);
	AdvanceSupportWorld(World, 0.02f);
	Assigned->Behavior->SetComponentTickEnabled(false);
	TestEqual(TEXT("갱신된 보고 시각으로 만료 판단"), FLSEnemySupportTestAccess::Pending(Assigned->Behavior), 1);
	AdvanceSupportWorld(World, 0.06f);
	Assigned->Behavior->SetComponentTickEnabled(true);
	AdvanceSupportWorld(World, 0.02f);
	Assigned->Behavior->SetComponentTickEnabled(false);
	TestEqual(TEXT("대기 요청 TTL이 지나면 제거"), FLSEnemySupportTestAccess::Pending(Assigned->Behavior), 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemySupportIncomingTest,
	"Landship.Enemies.Support.IncomingDamageSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemySupportIncomingTest::RunTest(const FString& Parameters)
{
	for (const bool bLethal : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = CreateSupportBattlefield(World);
		ALSEnemyOutpost* Reporter = SpawnSupportActor<ALSEnemyOutpost>(World, FVector(0, 0, 100));
		Reporter->BasicAttack->SetAutoAttackOnBeginPlay(false);
		FBoolProperty* Armed = FindFProperty<FBoolProperty>(ALSOutpost::StaticClass(), TEXT("bArmed"));
		if (!TestNotNull(TEXT("거점 무장 설정 필드"), Armed)) return false;
		Armed->SetPropertyValue_InContainer(Reporter, false);
		ALSEnemyTroop* Recipient = CreateSupportGuard(World, Reporter, FVector(500, 0, 100), 10);
		ALSBasicAttackTestActor* Source = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(2000, 0, 100));
		ALSBasicAttackTestActor* Causer = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(2500, 0, 100));
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		for (AActor* Actor : {static_cast<AActor*>(Setup->PlayerCastle), static_cast<AActor*>(Setup->CoreOutpost.Get()),
			static_cast<AActor*>(Reporter), static_cast<AActor*>(Recipient)}) { FreezeSupportActor(Actor); }
		FLSHealthSnapshot Health;
		Reporter->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
		const float Damage = bLethal ? Health.CurrentHealth : 1.0f;
		TestEqual(TEXT("실제 피해 적용"), Reporter->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
			ULSHealthComponent::MakeDamageRequest(Damage, Source, Causer)).AppliedDamage, Damage);
		TestEqual(TEXT("비무장 거점도 비치명·치명 피해에서 지원 요청"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 1);
		if (FLSEnemySupportTestAccess::Pending(Recipient->Behavior) != 1) return false;
		TestEqual(TEXT("피해 유발체 대신 실제 공격자를 지원 상대에 기록"),
			FLSEnemySupportTestAccess::Opponent(Recipient->Behavior), static_cast<AActor*>(Source));
		if (!bLethal)
		{
			ALSBasicAttackTestActor* RemovedSource = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(3000, 0, 100));
			FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(1, RemovedSource, Causer);
			RemovedSource->Destroy();
			TestEqual(TEXT("제거된 공격자의 보존 진영 피해는 적용"), Reporter->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(Request).AppliedDamage, 1.0f);
			TestEqual(TEXT("제거된 공격자로 새 지원 요청 생성하지 않음"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 1);
		}
		Reporter->Destroy();
		TestEqual(TEXT("요청자 제거 뒤에도 전달된 보고 보존"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 1);
		TestEqual(TEXT("요청자 제거 뒤에도 상대 저장 위치 보존"), FLSEnemySupportTestAccess::ReportedLocation(Recipient->Behavior), FVector(2000, 0, 100));
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemySupportOutgoingTest,
	"Landship.Enemies.Support.ZeroDamageAttackExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemySupportOutgoingTest::RunTest(const FString& Parameters)
{
	for (const bool bProjectile : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = CreateSupportBattlefield(World);
		ALSEnemyOutpost* Reporter = SpawnSupportActor<ALSEnemyOutpost>(World, FVector(0, -500, 100));
		Reporter->BasicAttack->SetAutoAttackOnBeginPlay(false);
		ALSEnemyTroop* Recipient = CreateSupportGuard(World, Reporter, FVector(500, -500, 100), 600);
		ALSBasicAttackTestActor* Opponent = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(100, -500, 100));
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		for (AActor* Actor : {static_cast<AActor*>(Setup->PlayerCastle), static_cast<AActor*>(Setup->CoreOutpost.Get()),
			static_cast<AActor*>(Reporter), static_cast<AActor*>(Recipient)}) { FreezeSupportActor(Actor); }
		FLSBasicAttackSettings Attack;
		Attack.Damage = 0;
		Attack.Interval = 1;
		Attack.Range = 300;
		TestTrue(TEXT("피해 0 공격 설정"), Reporter->BasicAttack->ConfigureAttack(Attack));
		FLSProjectileAttackSettings Projectile;
		if (bProjectile) { Projectile.ProjectileClass = ALSBasicProjectile::StaticClass(); }
		TestTrue(TEXT("실제 근접·투사체 경로 설정"), Reporter->BasicAttack->ConfigureProjectile(Projectile));
		float ReporterRadius, ReporterHeight, OpponentRadius, OpponentHeight;
		Reporter->GetSimpleCollisionCylinder(ReporterRadius, ReporterHeight);
		Opponent->GetSimpleCollisionCylinder(OpponentRadius, OpponentHeight);
		Opponent->SetActorLocation(Reporter->GetActorLocation() +
			FVector(Attack.Range + ReporterRadius + OpponentRadius + 100, 0, 0));
		TestTrue(TEXT("접근 단계는 실제 충돌 외곽 사거리 밖이다"),
			ULSCombatTargetLibrary::GetAttackDistance(Reporter, Opponent) > Attack.Range);
		TestTrue(TEXT("사거리 밖 접근 대상 지정"), Reporter->BasicAttack->StartAttack(Opponent));
		TestEqual(TEXT("접근만으로 지원을 만들지 않음"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 0);
		Opponent->SetActorLocation(FVector(200, -500, 100));
		TestTrue(TEXT("공격 단계는 실제 충돌 외곽 사거리 안이다"),
			ULSCombatTargetLibrary::GetAttackDistance(Reporter, Opponent) <= Attack.Range);
		TestTrue(TEXT("실제 공격 시작"), Reporter->BasicAttack->StartAttack(Opponent));
		TWeakObjectPtr<ALSBasicProjectile> LaunchedProjectile;
		if (bProjectile)
		{
			for (TActorIterator<ALSBasicProjectile> It(World); It; ++It) { LaunchedProjectile = *It; }
			TestTrue(TEXT("명중 전 실제 발사체가 존재함"), LaunchedProjectile.IsValid());
		}
		TestEqual(TEXT("실제 공격 확정 시 지원 요청 한 건"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 1);
		TestEqual(TEXT("피해 0이어도 상대 체력 유지"), Opponent->GetCurrentHealth(), 100.0f);
		if (FLSEnemySupportTestAccess::Pending(Recipient->Behavior) != 1) return false;
		TestEqual(TEXT("공격 실행 상대 기록"), FLSEnemySupportTestAccess::Opponent(Recipient->Behavior), static_cast<AActor*>(Opponent));
		const uint64 ExecutionRequest = FLSEnemySupportTestAccess::RequestId(Recipient->Behavior);
		Reporter->BasicAttack->StopAttack();
		AdvanceSupportWorld(World, 0.25f);
		if (bProjectile) { TestFalse(TEXT("수명 만료 전 실제 명중으로 발사체가 제거됨"), LaunchedProjectile.IsValid()); }
		TestEqual(TEXT("명중 때 지원 실행을 중복하지 않음"), FLSEnemySupportTestAccess::RequestId(Recipient->Behavior), ExecutionRequest);
		TestEqual(TEXT("피해 알림 없이 지원 요청 유지"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 1);
		TestEqual(TEXT("투사체 명중도 피해 0"), Opponent->GetCurrentHealth(), 100.0f);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemySupportRejectedLaunchTest,
	"Landship.Enemies.Support.RejectedSpawnCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemySupportRejectedLaunchTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBattlefieldSetup* Setup = CreateSupportBattlefield(World);
	ALSEnemyOutpost* Reporter = SpawnSupportActor<ALSEnemyOutpost>(World, FVector(0, -500, 100));
	Reporter->BasicAttack->SetAutoAttackOnBeginPlay(false);
	ALSEnemyTroop* Recipient = CreateSupportGuard(World, Reporter, FVector(500, -500, 100), 600);
	ALSBasicAttackTestActor* Opponent = SpawnSupportActor<ALSBasicAttackTestActor>(World, FVector(100, -500, 100));
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	for (AActor* Actor : {Setup->PlayerCastle.Get(), static_cast<AActor*>(Setup->CoreOutpost.Get()),
		static_cast<AActor*>(Reporter), static_cast<AActor*>(Recipient)}) { FreezeSupportActor(Actor); }
	FLSBasicAttackSettings Attack;
	Attack.Damage = 10;
	Attack.Interval = 1;
	Attack.Range = 300;
	TestTrue(TEXT("유효한 무장 거점 공격 설정"), Reporter->BasicAttack->ConfigureAttack(Attack));
	FLSProjectileAttackSettings Projectile;
	Projectile.ProjectileClass = ALSRejectedSupportProjectile::StaticClass();
	TestTrue(TEXT("생성 완료 중 제거되는 투사체 설정"), Reporter->BasicAttack->ConfigureProjectile(Projectile));
	const int32 BeforeRejections = ALSRejectedSupportProjectile::RejectionCount;
	TestTrue(TEXT("사거리 안 실제 공격 시도"), Reporter->BasicAttack->StartAttack(Opponent));
	Reporter->BasicAttack->StopAttack();
	TestEqual(TEXT("실제 투사체가 생성 완료 중 제거됨"), ALSRejectedSupportProjectile::RejectionCount, BeforeRejections + 1);
	int32 LiveRejectedProjectiles = 0;
	for (TActorIterator<ALSRejectedSupportProjectile> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed()) { ++LiveRejectedProjectiles; }
	}
	TestEqual(TEXT("실패한 투사체가 월드에 남지 않음"), LiveRejectedProjectiles, 0);
	TestEqual(TEXT("발사 확정 전 제거되면 지원을 만들지 않음"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 0);
	AdvanceSupportWorld(World, 0.1f);
	TestEqual(TEXT("실패한 발사에서 지연 지원도 없음"), FLSEnemySupportTestAccess::Pending(Recipient->Behavior), 0);
	TestEqual(TEXT("실패한 발사는 피해를 주지 않음"), Opponent->GetCurrentHealth(), 100.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}
#endif
