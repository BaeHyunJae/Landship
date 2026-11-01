#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleProjectileBlockingTest, "Landship.Combat.Projectile.ObstacleFirstBlocking",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleProjectileBlockingTest::RunTest(const FString& Parameters)
{
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
			TestEqual(TEXT("아군 몸체는 막지만 피해 없음"), Ally->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("발사자 초기 중첩 무시"), Source->GetCurrentHealth(), 100.f);
	}
	return true;
}
#endif
