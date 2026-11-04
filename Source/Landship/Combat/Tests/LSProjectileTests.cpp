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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileObstacleTest, "Landship.Combat.Projectile.BlocksPhysicalCover",
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
	TestEqual(TEXT("실제 장애물 뒤의 적은 피해를 받지 않음"), Enemy->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("장애물 뒤의 적에게 명중 알림 없음"), Enemy->HitsReceived, 0);
	TestFalse(TEXT("첫 실제 장애물에서 투사체 제거"), Shot.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileFactionTest, "Landship.Combat.Projectile.FirstAllowedCombatant",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileFactionTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginal = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginal;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
	};
	Settings->bAllowSelfDamage = true;
	for (ELSFaction SourceFaction : {ELSFaction::Ally, ELSFaction::Enemy})
	{
		for (bool bFriendlyFire : {false, true})
		{
			for (ECollisionChannel BodyType : {LSCollisionChannels::Unit, LSCollisionChannels::Building})
			{
				Settings->bAllowFriendlyFire = bFriendlyFire;
				FTestWorldWrapper TestWorld;
				if (!TestWorld.CreateTestWorld(EWorldType::Game))
					return false;
				UWorld* World = TestWorld.GetTestWorld();
				const ELSFaction EnemyFaction = SourceFaction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally;
				auto SpawnBody = [World](ELSFaction Faction, float X)
				{
					auto* Body = World->SpawnActor<ALSBasicAttackTestActor>();
					Body->SetInitialFaction(Faction);
					Body->SetActorLocation(FVector(X, 0, 100));
					Body->Attack->SetAutoAttackOnBeginPlay(false);
					return Body;
				};
				auto* Source = SpawnBody(SourceFaction, 0);
				auto* Intended = SpawnBody(EnemyFaction, 600);
				auto* FarEnemy = SpawnBody(EnemyFaction, 450);
				auto* OtherEnemy = SpawnBody(EnemyFaction, 300);
				auto* Ally = SpawnBody(SourceFaction, 200);
				for (auto* Body : {Ally, OtherEnemy})
					Body->FindComponentByClass<UPrimitiveComponent>()->SetCollisionObjectType(BodyType);
				if (!TestWorld.BeginPlayInTestWorld())
					return false;
				ConfigureShot(Source);
				Source->Attack->OnAttackPerformed.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
				TestTrue(TEXT("Explicit enemy target launches"), Source->Attack->StartAttack(Intended));
				Source->Attack->StopAttack();
				auto* Shot = FindShot(World);
				if (!TestNotNull(TEXT("Projectile exists"), Shot))
					return false;
				Shot->Tick(0.8f);
				ALSBasicAttackTestActor* Expected = bFriendlyFire ? Ally : OtherEnemy;
				TestEqual(TEXT("Nearest allowed combatant takes one hit in a large segment"), Expected->HitsReceived, 1);
				TestEqual(TEXT("Friendly interception follows current flag"), Ally->GetCurrentHealth(), bFriendlyFire ? 90.f : 100.f);
				TestEqual(TEXT("Other enemy intercepts when ally is passed"), OtherEnemy->GetCurrentHealth(), bFriendlyFire ? 100.f : 90.f);
				TestEqual(TEXT("Farther enemy is untouched"), FarEnemy->HitsReceived, 0);
				TestEqual(TEXT("Original target is untouched"), Intended->HitsReceived, 0);
				TestEqual(TEXT("Source initial overlap is ignored"), Source->HitsReceived, 0);
				TestTrue(TEXT("First accepted hit ends projectile"), Shot->IsActorBeingDestroyed());
				Shot->Tick(0.1f);
				TestEqual(TEXT("Resolved projectile cannot hit twice"), Expected->HitsReceived, 1);
				TestEqual(TEXT("Attack component forwards one actual damage notification"), Source->Notifications, 1);
				TestEqual(TEXT("Notification identifies intercepted actor"), Source->LastNotifiedTarget.Get(), static_cast<AActor*>(Expected));
				TestWorld.ForwardErrorMessages(this);
			}
		}
	}
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
	TestTrue(TEXT("Another shot launches after cooldown"), Source->Attack->StartAttack(Enemy));
	Source->Attack->StopAttack();
	Shot = FindShot(World);
	Enemy->Destroy();
	AdvanceProjectileWorld(World, 0.7f);
	TestTrue(TEXT("Removing original target leaves an unobstructed shot in flight"), Shot.IsValid());
	AdvanceProjectileWorld(World, 0.4f);
	TestFalse(TEXT("Targetless unobstructed shot expires at its original lifetime"), Shot.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
