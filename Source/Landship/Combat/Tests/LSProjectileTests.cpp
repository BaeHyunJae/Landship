#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicProjectile.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace
{
void AdvanceProjectileWorld(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}

FLSProjectileAttackSettings ShotSettings()
{
	FLSProjectileAttackSettings Settings;
	Settings.ProjectileClass = ALSBasicProjectile::StaticClass();
	Settings.Speed = 1000;
	Settings.Lifetime = 1;
	return Settings;
}

void ConfigureShot(ALSBasicAttackTestActor* Source)
{
	FLSBasicAttackSettings Attack;
	Attack.Damage = 10;
	Attack.Range = 1000;
	Attack.Interval = 1;
	Source->Attack->ConfigureAttack(Attack);
	Source->Attack->ConfigureProjectile(ShotSettings());
}

ALSBasicProjectile* FindShot(UWorld* World)
{
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileDelayedHitTest, "Landship.Combat.Projectile.DelayedSingleHit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileDelayedHitTest::RunTest(const FString& Parameters)
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
	Enemy->SetActorLocation(FVector(500, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureShot(Source);
	Source->Attack->OnAttackPerformed.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
	TestTrue(TEXT("투사체 발사 요청"), Source->Attack->StartAttack(Enemy));
	Source->Attack->StopAttack();
	TestEqual(TEXT("발사 즉시 피해 없음"), Enemy->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("발사와 피해 알림 구분"), Source->Notifications, 0);
	TWeakObjectPtr<ALSBasicProjectile> Shot = FindShot(World);
	TestTrue(TEXT("실제 투사체 생성"), Shot.IsValid());
	AdvanceProjectileWorld(World, 0.25f);
	TestEqual(TEXT("비행 중 피해 없음"), Enemy->GetCurrentHealth(), 100.0f);
	TestTrue(TEXT("비행 중 실제 위치 이동"), Shot.IsValid() && Shot->GetActorLocation().X > 200);
	AdvanceProjectileWorld(World, 0.35f);
	TestEqual(TEXT("명중 후 공통 피해"), Enemy->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("명중 시 피해 알림 한 번"), Source->Notifications, 1);
	TestFalse(TEXT("명중 후 투사체 제거"), Shot.IsValid());
	AdvanceProjectileWorld(World, 0.5f);
	TestEqual(TEXT("명령 중단 이후에도 이미 발사한 화살은 한 번만 명중"), Enemy->HitsReceived, 1);
	FLSProjectileAttackSettings Invalid = ShotSettings();
	Invalid.Speed = 0;
	TestFalse(TEXT("0 속도 설정 거부"), Source->Attack->ConfigureProjectile(Invalid));
	Invalid = ShotSettings();
	Invalid.Lifetime = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN 수명 거부"), Source->Attack->ConfigureProjectile(Invalid));
	TestEqual(TEXT("실패 시 기존 속도 유지"), Source->Attack->GetProjectileSettings().Speed, 1000.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileObstacleTest, "Landship.Combat.Projectile.IgnoresObstacles",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileObstacleTest::RunTest(const FString& Parameters)
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
	Enemy->SetActorLocation(FVector(500, 0, 100));
	const ECollisionChannel ObstacleTypes[] = {LSCollisionChannels::PathObstacle, LSCollisionChannels::Ground,
	                                           ECC_WorldStatic, ECC_WorldDynamic, LSCollisionChannels::Building};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ObstacleTypes); ++Index)
	{
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Wall->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(10, 100, 100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ObstacleTypes[Index]);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Wall->SetActorLocation(FVector(80 + Index * 80, 0, 100));
	}
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureShot(Source);
	Source->Attack->StartAttack(Enemy);
	Source->Attack->StopAttack();
	TWeakObjectPtr<ALSBasicProjectile> Shot = FindShot(World);
	// 월드의 최대 DeltaTime 제한과 무관하게 적까지 도달할 비행 시간을 진행한다.
	AdvanceProjectileWorld(World, 0.8f);
	TestEqual(TEXT("지형·장애물·장식물을 통과해 뒤의 적에게 피해 적용"), Enemy->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("장애물 뒤 실제 전투 대상에 한 번 명중"), Enemy->HitsReceived, 1);
	TestFalse(TEXT("장애물이 아닌 적에게 명중한 후 투사체 제거"), Shot.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileFactionTest, "Landship.Combat.Projectile.PassesOtherCombatants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileFactionTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginal = Settings->bAllowFriendlyFire;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginal;
	};
	Settings->bAllowFriendlyFire = false;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Ally = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* OtherEnemy = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Building = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	OtherEnemy->SetInitialFaction(ELSFaction::Enemy);
	Building->SetInitialFaction(ELSFaction::Enemy);
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Building->FindComponentByClass<UPrimitiveComponent>()->SetCollisionObjectType(LSCollisionChannels::Building);
	Source->SetActorLocation(FVector(0, 0, 100));
	Ally->SetActorLocation(FVector(200, 0, 100));
	OtherEnemy->SetActorLocation(FVector(300, 0, 100));
	Building->SetActorLocation(FVector(400, 0, 100));
	Enemy->SetActorLocation(FVector(500, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	ConfigureShot(Source);
	Source->Attack->StartAttack(Enemy);
	Source->Destroy();
	AdvanceProjectileWorld(World, 0.6f);
	TestEqual(TEXT("아군 병력 통과"), Ally->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("목표 외 적 병력 통과"), OtherEnemy->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("목표 외 전투 건물 통과"), Building->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("공격자 제거 뒤에도 지정 목표에만 피해"), Enemy->GetCurrentHealth(), 90.0f);
	TestNull(TEXT("지정 목표 명중 후 화살 제거"), FindShot(World));
	ALSBasicAttackTestActor* NextSource = World->SpawnActor<ALSBasicAttackTestActor>();
	NextSource->SetActorLocation(FVector(0, 0, 100));
	ConfigureShot(NextSource);
	NextSource->Attack->StartAttack(Enemy);
	NextSource->Attack->StopAttack();
	Settings->bAllowFriendlyFire = true;
	AdvanceProjectileWorld(World, 0.6f);
	TestEqual(TEXT("오폭 허용 상태도 목표 외 아군에 피해 없음"), Ally->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("다른 적과 건물을 통과해 목표에 재명중"), Enemy->GetCurrentHealth(), 80.0f);
	TestEqual(TEXT("목표 외 적은 반복 사격에도 피해 없음"), OtherEnemy->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("목표 외 건물은 반복 사격에도 피해 없음"), Building->GetCurrentHealth(), 100.0f);
	AdvanceProjectileWorld(World, 0.45f);
	NextSource->Attack->StartAttack(Enemy);
	NextSource->Attack->StopAttack();
	Enemy->Destroy();
	AdvanceProjectileWorld(World, 0.7f);
	TestNotNull(TEXT("목표 제거 시 다른 대상으로 바꾸지 않고 직진"), FindShot(World));
	TestEqual(TEXT("목표 제거 후에도 다른 적에 피해 없음"), OtherEnemy->GetCurrentHealth(), 100.0f);
	AdvanceProjectileWorld(World, 0.4f);
	TestNull(TEXT("목표 없는 투사체도 수명 종료 시 제거"), FindShot(World));
	TestWorld.ForwardErrorMessages(this);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileMissTest, "Landship.Combat.Projectile.MissAndLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileMissTest::RunTest(const FString& Parameters)
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
	Enemy->SetActorLocation(FVector(500, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	ConfigureShot(Source);
	Source->Attack->StartAttack(Enemy);
	Source->Attack->StopAttack();
	TWeakObjectPtr<ALSBasicProjectile> Shot = FindShot(World);
	Enemy->SetActorLocation(FVector(500, 200, 100));
	AdvanceProjectileWorld(World, 0.7f);
	TestEqual(TEXT("발사 후 옆으로 이동하면 비유도 화살 회피"), Enemy->GetCurrentHealth(), 100.0f);
	TestTrue(TEXT("빗나간 화살은 계속 비행"), Shot.IsValid());
	AdvanceProjectileWorld(World, 0.5f);
	TestFalse(TEXT("수명 종료 시 투사체 제거"), Shot.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
