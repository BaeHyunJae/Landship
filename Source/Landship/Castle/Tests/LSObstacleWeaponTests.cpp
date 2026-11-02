#if WITH_DEV_AUTOMATION_TESTS
#include "Castle/LSCastleProjectile.h"
#include "Troops/LSTroopUnit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Obstacles/LSMine.h"
#include "EngineUtils.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Obstacles/LSBarricade.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleShotgunPelletsTest,
								 "Landship.Castle.Projectiles.ObstaclePelletsTerminateIndependently",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleShotgunPelletsTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	Source->SetActorLocation(FVector(0, 0, 100));
	Source->Attack->SetAutoAttackOnBeginPlay(false);
	TArray<ALSBasicAttackTestActor*> Enemies;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Target = World->SpawnActor<ALSBasicAttackTestActor>();
		Target->SetInitialFaction(ELSFaction::Enemy);
		Target->Attack->SetAutoAttackOnBeginPlay(false);
		Target->SetActorLocation(FVector(0, 0, 100) +
								 FVector::ForwardVector.RotateAngleAxis(-20.f + Index * 20.f, FVector::UpVector) * 400);
		CastChecked<USphereComponent>(Target->GetRootComponent())
			->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
		Enemies.Add(Target);
	}
	AActor* Wall = World->SpawnActor<AActor>();
	auto* Body = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Body);
	Wall->AddInstanceComponent(Body);
	Body->SetBoxExtent(FVector(1, 8, 50));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->RegisterComponent();
	Wall->SetActorLocation(FVector(200, 0, 100));
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	FLSCastleWeaponSettings Settings;
	Settings.PelletCount = 3;
	Settings.ConeAngle = 40;
	Settings.Range = 600;
	Settings.ProjectileSpeed = 10000;
	Settings.Damage = 30;
	Settings.PelletRadius = 2;
	const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 100));
	auto* Shot =
		World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr,
													   ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestTrue(TEXT("실제 몸체 산탄 발사"),
				  Shot->Launch(ELSCastleWeapon::Shotgun, Settings,
							   ULSHealthComponent::MakeDamageRequest(30, Source, nullptr), FVector(400, 0, 100), true)))
		return false;
	Shot->FinishSpawning(Transform);
	Shot->Tick(.1f);
	TestEqual(TEXT("왼쪽 탄 독립 명중"), Enemies[0]->GetCurrentHealth(), 90.f);
	TestEqual(TEXT("중앙 탄은 벽에서 종료"), Enemies[1]->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("오른쪽 탄 독립 명중"), Enemies[2]->GetCurrentHealth(), 90.f);
	TestTrue(TEXT("모든 탄 종료"), Shot->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleCannonEdgeTest, "Landship.Castle.Projectiles.CannonRotatedBarricadeEdge",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleCannonEdgeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	Source->Attack->SetAutoAttackOnBeginPlay(false);
	auto* Barrier = World->SpawnActor<ALSBarricade>();
	auto* Body = CastChecked<UBoxComponent>(Barrier->GetObstacleBody());
	Body->SetBoxExtent(FVector(30, 600, 90));
	Barrier->SetActorLocation(FVector(1000, 0, 90));
	Barrier->SetActorRotation(FRotator(0, 45, 0));
	const FVector Aim = Body->GetComponentTransform().TransformPosition(FVector(0, 580, 0)) - FVector(0, 0, 500);
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	FLSHealthSnapshot Before;
	TestTrue(TEXT("바리케이드 체력 준비"), Barrier->GetHealthComponent()->TryGetHealth(Before));
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 60;
	Settings.Radius = 100;
	const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 150));
	auto* Shot =
		World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr,
													   ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestTrue(TEXT("긴 바리케이드 끝 조준"),
				  Shot->Launch(ELSCastleWeapon::Cannon, Settings,
							   ULSHealthComponent::MakeDamageRequest(60, Source, nullptr), Aim)))
		return false;
	Shot->FinishSpawning(Transform);
	Shot->Tick(10);
	FLSHealthSnapshot After;
	TestTrue(TEXT("착탄 뒤 체력 조회"), Barrier->GetHealthComponent()->TryGetHealth(After));
	TestEqual(TEXT("액터 원점 밖 몸체 끝에 범위 피해"), After.CurrentHealth, Before.CurrentHealth - 60);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleLowMineAimTest, "Landship.Castle.Projectiles.ObstacleAimLowMine",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleLowMineAimTest::RunTest(const FString& Parameters)
{
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game))
			return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Castle = World->SpawnActor<ALSMobileCastle>();
		Castle->SetActorLocation(FVector(0, 0, 150));
		auto* Mine = World->SpawnActor<ALSMine>();
		Mine->SetActorLocation(FVector(400, 0, 20));
		if (!Fixture.BeginPlayInTestWorld())
			return false;
		Castle->Weapons->UnlockWeapon(Weapon);
		Castle->Weapons->SetWeaponEnabled(Weapon, true);
		FVector Aim;
		TestTrue(TEXT("실제 구 몸체 조준점"), Castle->Weapons->GetObstacleAimPoint(Mine, Aim));
		TestTrue(TEXT("높이를 낮춘 조준점"), Aim.Equals(Mine->GetObstacleBody()->Bounds.Origin));
		if (!TestTrue(TEXT("장애물 조준 발사"),
					  Castle->Weapons->CastWeaponAtObstacle(Weapon, Mine) == ELSWeaponCastResult::Ready))
			return false;
		for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
			It->Tick(1);
		for (TActorIterator<ALSCastleProjectile> It(World); It; ++It)
			It->Tick(1);
		TestFalse(TEXT("낮은 실제 몸체 명중으로 지뢰 발동"), Mine->IsArmed());
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineContactNotProjectileBodyTest,
    "Landship.Castle.Projectiles.MineContactIsNotAttackBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMineContactNotProjectileBodyTest::RunTest(const FString& Parameters)
{
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
		Source->Attack->SetAutoAttackOnBeginPlay(false);
		auto* Mine = World->SpawnActor<ALSMine>();
		Mine->SetActorLocation(FVector(400, 0, 20));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		const FVector Start(0, Mode == 0 ? 0 : 70, 20);
		const FTransform Transform(FRotator::ZeroRotator, Start);
		const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(10, Source, nullptr);
		if (Mode < 3)
		{
			auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform,
			    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			const bool bLaunched = Mode < 2
			    ? Shot->Launch(Request, Mine, FVector::ForwardVector, 1000, 2, 2)
			    : Shot->LaunchInDirection(Request, FVector::ForwardVector, 1000, 2, 2);
			if (!TestTrue(TEXT("기존 투사체 발사"), bLaunched)) return false;
			Shot->FinishSpawning(Transform);
			Shot->Tick(.34f);
			TestTrue(TEXT("접촉 구만 진입한 동안 지뢰 유지"), Mine->IsArmed());
			TestFalse(TEXT("접촉 구에서 탄 종료 없음"), Shot->IsActorBeingDestroyed());
			Shot->Tick(.2f);
			TestEqual(TEXT("지정 화살은 실제 몸체에 닿을 때만 발동"), Mine->IsArmed(), Mode != 0);
		}
		else
		{
			FLSCastleWeaponSettings Settings;
			Settings.PelletCount = 1;
			Settings.ProjectileSpeed = 1000;
			Settings.Range = 700;
			Settings.PelletRadius = 2;
			auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
			    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!TestTrue(TEXT("일반 좌표 산탄 발사"), Shot->Launch(ELSCastleWeapon::Shotgun, Settings, Request,
			    Start + FVector(700, 0, 0)))) return false;
			Shot->FinishSpawning(Transform);
			Shot->Tick(.6f);
			TestTrue(TEXT("일반 산탄은 접촉 구를 지나도 발동 없음"), Mine->IsArmed());
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSOrdinaryShotgunKnockbackDirectionTest,
    "Landship.Castle.Projectiles.OrdinaryShotgunKeepsRadialKnockback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSOrdinaryShotgunKnockbackDirectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	Source->Attack->SetAutoAttackOnBeginPlay(false);
	auto* Enemy = World->SpawnActor<ALSTroopUnit>();
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Enemy->SetActorLocation(FVector(400, 30, 100));
	Enemy->BasicAttack->SetAutoAttackOnBeginPlay(false);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	const FVector Start(0, 0, 100);
	FLSCastleWeaponSettings Settings;
	Settings.PelletCount = 1;
	Settings.Range = 600;
	Settings.ProjectileSpeed = 1000;
	Settings.Damage = 1;
	Settings.PelletRadius = 2;
	const FTransform Transform(FRotator::ZeroRotator, Start);
	auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
	    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestTrue(TEXT("일반 산탄 가장자리 명중 발사"), Shot->Launch(ELSCastleWeapon::Shotgun, Settings,
	    ULSHealthComponent::MakeDamageRequest(1, Source, nullptr), Start + FVector(600, 0, 0)))) return false;
	Shot->FinishSpawning(Transform);
	Shot->Tick(.5f);
	const FVector Knockback = Enemy->GetCharacterMovement()->PendingLaunchVelocity;
	TestTrue(TEXT("실제 병력 넉백 발생"), !Knockback.IsNearlyZero());
	TestTrue(TEXT("일반 산탄은 탄 방향 대신 액터 중심 방향 유지"),
	    Knockback.GetSafeNormal2D().Equals((Enemy->GetActorLocation() - Start).GetSafeNormal2D(), .001));
	return true;
}
#endif
