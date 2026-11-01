#if WITH_DEV_AUTOMATION_TESTS
#include "Castle/LSCastleProjectile.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

namespace
{
ALSBasicAttackTestActor* MakeProjectileTarget(UWorld* World, FVector Location, ELSFaction Faction = ELSFaction::Enemy)
{
	auto* Target = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Target->SetInitialFaction(Faction);
	Target->Attack->SetAutoAttackOnBeginPlay(false);
	Target->HealthComponent->bDestroyOnDeath = false;
	return Target;
}
void StepProjectileWorld(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}
ALSCastleProjectile* FindCastleProjectile(UWorld* World)
{
	for (TActorIterator<ALSCastleProjectile> It(World); It; ++It)
		return *It;
	return nullptr;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSArcSnapshotTest, "Landship.Castle.Projectiles.ArcAndSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSArcSnapshotTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Castle = World->SpawnActor<ALSMobileCastle>();
	auto* Inside = MakeProjectileTarget(World, FVector(800, 0, 0));
	auto* Dodges = MakeProjectileTarget(World, FVector(800, 20, 0));
	auto* Enters = MakeProjectileTarget(World, FVector(1200, 0, 0));
	auto* Ally = MakeProjectileTarget(World, FVector(800, -20, 0), ELSFaction::Ally);
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	TestTrue(TEXT("곡사 발사"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, FVector(800, 0, 0)) == ELSWeaponCastResult::Ready);
	auto* Shell = FindCastleProjectile(World);
	if (!TestNotNull(TEXT("포탄 생성"), Shell))
		return false;
	const float StartZ = Shell->GetActorLocation().Z;
	TestEqual(TEXT("즉시 피해 없음"), Inside->GetCurrentHealth(), 100.0f);
	Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false);
	StepProjectileWorld(World, 0.15f);
	TestTrue(TEXT("곡사 궤적 상승"), Shell->GetActorLocation().Z > StartZ);
	TestEqual(TEXT("비행 중 피해 없음"), Inside->GetCurrentHealth(), 100.0f);
	Dodges->SetActorLocation(FVector(1300, 0, 0));
	Enters->SetActorLocation(FVector(800, 50, 0));
	Castle->Destroy();
	StepProjectileWorld(World, 0.4f);
	TestEqual(TEXT("강화·비활성·발사자 제거 후에도 발사 당시 피해"), Inside->GetCurrentHealth(), 40.0f);
	TestEqual(TEXT("착탄 전 들어온 적 피해"), Enters->GetCurrentHealth(), 40.0f);
	TestEqual(TEXT("착탄 전 이탈한 적 회피"), Dodges->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("아군 제외"), Ally->GetCurrentHealth(), 100.0f);
	TestNull(TEXT("착탄 후 포탄 제거"), FindCastleProjectile(World));
	StepProjectileWorld(World, 0.2f);
	TestEqual(TEXT("중복 피해 없음"), Inside->GetCurrentHealth(), 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunSnapshotTest, "Landship.Castle.Projectiles.PelletsAndLifetime",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSShotgunSnapshotTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = MakeProjectileTarget(World, FVector::ZeroVector, ELSFaction::Ally);
	auto* Ally = MakeProjectileTarget(World, FVector(100, 0, 0), ELSFaction::Ally);
	auto* Near = MakeProjectileTarget(World, FVector(250, 0, 0));
	auto* Behind = MakeProjectileTarget(World, FVector(400, 0, 0));
	auto* Outside = MakeProjectileTarget(World, FVector(700, 0, 0));
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 39;
	Settings.PelletCount = 13;
	Settings.Range = 600;
	const FTransform Transform(FVector::ZeroVector);
	auto* Volley =
	    World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr,
	                                                   ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	TestTrue(TEXT("산탄 묶음 발사"),
	         Volley->Launch(ELSCastleWeapon::Shotgun, Settings,
	                        ULSHealthComponent::MakeDamageRequest(39, Source, Source), FVector(600, 0, 0)));
	Volley->FinishSpawning(Transform);
	TArray<UStaticMeshComponent*> Bullets;
	Volley->GetComponents(Bullets);
	TestEqual(TEXT("개별 탄환 메시 수"), Bullets.Num(), 13);
	TestEqual(TEXT("즉시 피해 없음"), Near->GetCurrentHealth(), 100.0f);
	Source->Destroy();
	StepProjectileWorld(World, 0.4f);
	TestEqual(TEXT("중앙 탄환 피해는 총피해/탄환수"), Near->GetCurrentHealth(), 97.0f);
	TestEqual(TEXT("탄환이 처음 적에게 닿으면 소멸"), Behind->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("아군 통과"), Ally->GetCurrentHealth(), 100.0f);
	TestEqual(TEXT("최대 거리 밖 제외"), Outside->GetCurrentHealth(), 100.0f);
	TestNull(TEXT("최대 거리 이후 묶음 제거"), FindCastleProjectile(World));
	return true;
}
#endif
