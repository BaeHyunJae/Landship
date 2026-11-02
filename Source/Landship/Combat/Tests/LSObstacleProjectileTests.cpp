#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSMine.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleProjectileBlockingTest, "Landship.Combat.Projectile.ObstacleFirstBlocking",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleProjectileBlockingTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
	};
	Settings->bAllowSelfDamage = true;
	for (bool bFriendlyFire : {false, true})
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		for (int32 Scenario = 0; Scenario < 5; ++Scenario)
		{
			FTestWorldWrapper Fixture;
			if (!Fixture.CreateTestWorld(EWorldType::Game))
				return false;
			UWorld* World = Fixture.GetTestWorld();
			auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
			auto* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
			Source->SetActorLocation(FVector(0, 0, 100));
			Enemy->SetActorLocation(FVector(400, 0, 100));
			Enemy->SetInitialFaction(ELSFaction::Enemy);
			Enemy->Attack->SetAutoAttackOnBeginPlay(false);
			Source->Attack->SetAutoAttackOnBeginPlay(false);
			CastChecked<UPrimitiveComponent>(Source->GetRootComponent())
				->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
			CastChecked<UPrimitiveComponent>(Enemy->GetRootComponent())
				->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
			ALSBasicAttackTestActor* Ally = nullptr;
			if (Scenario == 1 || Scenario == 2)
			{
				Ally = World->SpawnActor<ALSBasicAttackTestActor>();
				Ally->Attack->SetAutoAttackOnBeginPlay(false);
				Ally->SetActorLocation(FVector(Scenario == 2 ? 0 : 200, 0, 100));
				CastChecked<UPrimitiveComponent>(Ally->GetRootComponent())
					->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
			}
			if (Scenario == 0 || Scenario == 3)
			{
				AActor* Wall = World->SpawnActor<AActor>();
				auto* Body = NewObject<UBoxComponent>(Wall);
				Wall->SetRootComponent(Body);
				Wall->AddInstanceComponent(Body);
				Body->SetBoxExtent(Scenario == 0 ? FVector(1, 100, 100) : FVector(100, 100, 1));
				Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Body->SetCollisionObjectType(Scenario == 0 ? ECC_WorldStatic : LSCollisionChannels::Ground);
				Body->SetCollisionResponseToAllChannels(ECR_Ignore);
				Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
				Body->RegisterComponent();
				Wall->SetActorLocation(FVector(200, 0, 100));
			}
			if (!Fixture.BeginPlayInTestWorld())
				return false;
			const FVector Start(0, 0, 100);
			const FTransform Transform(FRotator::ZeroRotator, Start);
			auto* Shot =
				World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform, nullptr,
															  nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			TestTrue(TEXT("첫 몸체 충돌 모드 발사"),
					 Shot->LaunchFirstBlocking(ULSHealthComponent::MakeDamageRequest(10, Source, nullptr),
											   FVector::ForwardVector, 10000, 2, 1));
			Shot->FinishSpawning(Transform);
			Shot->Tick(.1f);
			TestTrue(TEXT("고속 한 구간에서 최초 충돌 후 종료"), Shot->IsActorBeingDestroyed());
			TestEqual(TEXT("몸체가 없을 때만 뒤쪽 적 명중"), Enemy->GetCurrentHealth(), Scenario == 4 ? 90.f : 100.f);
			if (Ally)
				TestEqual(TEXT("Ally body always blocks; friendly fire controls damage"), Ally->GetCurrentHealth(), bFriendlyFire ? 90.f : 100.f);
			TestEqual(TEXT("발사자 초기 중첩 무시"), Source->GetCurrentHealth(), 100.f);
			Fixture.ForwardErrorMessages(this);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileFriendlyObstacleTest, "Landship.Combat.Projectile.EnemyFriendlyObstacles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileFriendlyObstacleTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
	};
	Settings->bAllowSelfDamage = true;
	for (bool bFriendlyFire : {false, true})
	{
		for (bool bMine : {false, true})
		{
			Settings->bAllowFriendlyFire = bFriendlyFire;
			FTestWorldWrapper Fixture;
			if (!Fixture.CreateTestWorld(EWorldType::Game))
				return false;
			UWorld* World = Fixture.GetTestWorld();
			auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
			Source->SetInitialFaction(ELSFaction::Enemy);
			Source->Attack->SetAutoAttackOnBeginPlay(false);
			Source->SetActorLocation(FVector(0, 0, 100));
			auto* Intended = World->SpawnActor<ALSBasicAttackTestActor>();
			Intended->Attack->SetAutoAttackOnBeginPlay(false);
			Intended->SetActorLocation(FVector(800, 0, 100));
			ALSPathObstacle* Obstacle = bMine ? static_cast<ALSPathObstacle*>(World->SpawnActor<ALSMine>())
				: static_cast<ALSPathObstacle*>(World->SpawnActor<ALSBarricade>());
			Obstacle->SetActorLocation(FVector(350, 0, 100));
			if (!Fixture.BeginPlayInTestWorld())
				return false;
			FLSHealthSnapshot Before;
			TestTrue(TEXT("Real obstacle health initialized"), Obstacle->GetHealthComponent()->TryGetHealth(Before));
			const FTransform Transform(FRotator::ZeroRotator, Source->GetActorLocation());
			auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!TestTrue(TEXT("Enemy shot launched past its own obstacle"),
				Shot->Launch(ULSHealthComponent::MakeDamageRequest(10, Source, nullptr), Intended,
					FVector::ForwardVector, 1000, 2, 2)))
				return false;
			Shot->OnDamageApplied.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
			Shot->FinishSpawning(Transform);
			Shot->Tick(0.9f);
			FLSHealthSnapshot After;
			TestTrue(TEXT("Obstacle health remains readable"), Obstacle->GetHealthComponent()->TryGetHealth(After));
			TestEqual(TEXT("Enemy obstacle receives friendly damage only when allowed"), After.CurrentHealth,
				Before.CurrentHealth - (bFriendlyFire ? 10.f : 0.f));
			TestEqual(TEXT("Disabled friendly fire passes obstacle to opponent"), Intended->GetCurrentHealth(), bFriendlyFire ? 100.f : 90.f);
			TestEqual(TEXT("Hit notification names actual obstacle or opponent"), Source->LastNotifiedTarget.Get(),
				bFriendlyFire ? static_cast<AActor*>(Obstacle) : static_cast<AActor*>(Intended));
			TestEqual(TEXT("One projectile damage notification"), Source->Notifications, 1);
			TestTrue(TEXT("First eligible body consumes projectile"), Shot->IsActorBeingDestroyed());
			if (auto* Mine = Cast<ALSMine>(Obstacle))
				TestEqual(TEXT("Only positive friendly damage triggers real mine"), Mine->GetMineState(),
					bFriendlyFire ? ELSMineState::Spent : ELSMineState::Armed);
			Fixture.ForwardErrorMessages(this);
		}
	}
	return true;
}
#endif
