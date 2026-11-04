#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastleProjectile.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSOutpost.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

namespace
{
ALSBasicAttackTestActor* MakeCoverTarget(UWorld* World, const FVector& Location, ELSFaction Faction = ELSFaction::Enemy)
{
	auto* Target = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Target->SetInitialFaction(Faction);
	Target->Attack->SetAutoAttackOnBeginPlay(false);
	Target->HealthComponent->bDestroyOnDeath = false;
	return Target;
}

AActor* MakeCoverWall(UWorld* World, const FVector& Location, const FVector& Extent)
{
	AActor* Actor = World->SpawnActor<AActor>();
	UBoxComponent* Body = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Body);
	Actor->AddInstanceComponent(Body);
	Body->SetBoxExtent(Extent);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	Body->SetCanEverAffectNavigation(false);
	Body->RegisterComponent();
	Actor->SetActorLocation(Location);
	return Actor;
}

ALSCastleProjectile* LaunchCoverShot(UWorld* World, AActor* Source, ELSCastleWeapon Kind,
	const FVector& Start, const FVector& Aim, const FLSCastleWeaponSettings& Settings, bool bFirstBlocking = false)
{
	const FTransform Transform(Start);
	auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Shot->Launch(Kind, Settings, ULSHealthComponent::MakeDamageRequest(Settings.Damage, Source, nullptr), Aim, bFirstBlocking))
	{
		Shot->Destroy();
		return nullptr;
	}
	Shot->FinishSpawning(Transform);
	return Shot;
}

FLSCastleWeaponSettings CoverSettings()
{
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 20;
	Settings.ProjectileSpeed = 1000;
	Settings.Range = 1200;
	Settings.Radius = 120;
	Settings.ArcHeight = 300;
	Settings.ShellRadius = 10;
	Settings.PelletCount = 1;
	Settings.PelletRadius = 2;
	Settings.KnockbackSpeed = 0;
	Settings.KnockbackLift = 0;
	return Settings;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleArcCoverTest,
	"Landship.Castle.Cover.CannonActualArcTallWallAndLowCoverSlowFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleArcCoverTest::RunTest(const FString& Parameters)
{
	for (const double OffsetX : {0.0, 6000.0, 20000.0})
	for (const bool bTall : {false, true})
	for (const bool bSlowFrame : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		const FVector Translation(OffsetX, 0, 0);
		auto* Source = MakeCoverTarget(World, Translation + FVector(-500, 0, 100), ELSFaction::Ally);
		auto* Target = MakeCoverTarget(World, Translation + FVector(1000, 0, 100));
		auto* NearImpact = MakeCoverTarget(World, Translation + FVector(430, 0, 400));
		auto* FarImpact = MakeCoverTarget(World, Translation + FVector(550, 0, 400));
		auto* AirUnit = MakeCoverTarget(World, Translation + FVector(500, 0, 400));
		CastChecked<UPrimitiveComponent>(AirUnit->GetRootComponent())->SetCollisionResponseToChannel(
			LSCollisionChannels::ObstacleProjectile, ECR_Block);
		MakeCoverWall(World, Translation + FVector(500, 0, bTall ? 300 : 100), FVector(10, 200, bTall ? 300 : 100));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon,
			Translation + FVector(0, 0, 100), Translation + FVector(1000, 0, 100), CoverSettings());
		if (!TestNotNull(TEXT("실제 곡사 포탄 발사"), Shot)) return false;
		AddInfo(FString::Printf(TEXT("ArcCover OffsetX=%.0f Tall=%d SlowFrame=%d"), OffsetX, bTall, bSlowFrame));
		for (int32 Step = 0; Step < 200 && !Shot->IsActorBeingDestroyed(); ++Step)
			Shot->Tick(bSlowFrame ? TNumericLimits<float>::Max() : .01f);
		TestTrue(TEXT("벽 충돌 또는 목적지에서 포탄 종료"), Shot->IsActorBeingDestroyed());
		TestEqual(TEXT("높은 벽은 목적지 피해를 막고 낮은 벽은 곡사로 넘는다"), Target->GetCurrentHealth(), bTall ? 100.f : 80.f);
		TestEqual(TEXT("높은 벽에서는 실제 접촉점 주변에 폭발한다"), NearImpact->GetCurrentHealth(), bTall ? 80.f : 100.f);
		TestEqual(bTall ? TEXT("반경 안에 있는 벽 뒤 대상은 실제 차폐로 보호") : TEXT("낮은 벽을 넘긴 착탄점에서 먼 중간 대상은 범위 밖"),
			FarImpact->GetCurrentHealth(), 100.f);
		if (bTall)
			TestTrue(TEXT("벽 뒤 대상은 폭발 반경 밖이라서 제외된 경우가 아님"),
				FVector::Dist2D(FarImpact->GetActorLocation(), Shot->GetActorLocation()) < CoverSettings().Radius);
		if (!bTall)
			TestEqual(TEXT("비행 궤적 위 병력 몸체는 포탄을 가로막지 않는다"), AirUnit->GetCurrentHealth(), 100.f);
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleBlastCoverTest,
	"Landship.Castle.Cover.CannonBlastUsesActualSurfaceAndAdjacentWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleBlastCoverTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = MakeCoverTarget(World, FVector(-500, 0, 100), ELSFaction::Ally);
	auto* Front = MakeCoverTarget(World, FVector(30, 0, 100));
	auto* Behind = MakeCoverTarget(World, FVector(100, 0, 100));
	auto* Adjacent = MakeCoverTarget(World, FVector(65, 0, 100));
	auto* Open = MakeCoverTarget(World, FVector(100, 300, 100));
	auto* ThroughUnit = MakeCoverTarget(World, FVector(100, -300, 100));
	auto* UnitScreen = MakeCoverTarget(World, FVector(50, -150, 100));
	CastChecked<UPrimitiveComponent>(UnitScreen->GetRootComponent())->SetCollisionResponseToChannel(
		LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	MakeCoverWall(World, FVector(50, 0, 100), FVector(5, 100, 100));
	if (!Fixture.BeginPlayInTestWorld()) return false;
	FLSCastleWeaponSettings Settings = CoverSettings();
	Settings.Radius = 400;
	Settings.ArcHeight = 0;
	auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon,
		FVector(0, 0, 100), FVector(0, 0, 100), Settings);
	if (!TestNotNull(TEXT("차폐 검사 포탄 발사"), Shot)) return false;
	Shot->Tick(10);
	TestEqual(TEXT("벽 앞 대상 피해"), Front->GetCurrentHealth(), 80.f);
	TestEqual(TEXT("벽 뒤 대상 보호"), Behind->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("대상 표면과 벽 뒤 면이 맞닿아도 실제 벽은 차폐한다"), Adjacent->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("벽 옆으로 열린 대상 피해"), Open->GetCurrentHealth(), 80.f);
	TestEqual(TEXT("병력 몸체를 폭발 차폐물로 쓰지 않음"), ThroughUnit->GetCurrentHealth(), 80.f);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleArcCostBoundTest,
	"Landship.Castle.Cover.CannonRejectsUnboundedArcTraceCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleArcCostBoundTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = MakeCoverTarget(World, FVector(-500, 0, 100), ELSFaction::Ally);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	const auto* Canonical = LoadObject<ULSCastleWeaponData>(nullptr,
		TEXT("/Game/Landship/Castle/Data/DA_Cannon.DA_Cannon"));
	if (!TestNotNull(TEXT("기존 대포 데이터"), Canonical)) return false;
	TestTrue(TEXT("기존 대포의 모든 레벨은 같은 분할 기준으로 유효"), Canonical->IsDefinitionValid());
	for (const FLSCastleWeaponSettings& Settings : Canonical->Levels)
	{
		int32 Steps = 0;
		TestTrue(TEXT("기존 대포 수치 검증"), ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Cannon, Settings));
		TestTrue(TEXT("기존 대포 분할 수 계산"), ULSCastleWeaponData::TryGetCannonArcSteps(Settings, Steps));
		TestTrue(TEXT("기존 대포는 분할 비용 상한 이내"), Steps >= 1 && Steps <= 4096);
	}
	FLSCastleWeaponSettings ZeroArc = CoverSettings();
	ZeroArc.ArcHeight = 0;
	int32 ZeroArcSteps = 0;
	TestTrue(TEXT("높이 0의 대포 수치도 유효"), ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Cannon, ZeroArc));
	TestTrue(TEXT("높이 0의 분할 수 계산"), ULSCastleWeaponData::TryGetCannonArcSteps(ZeroArc, ZeroArcSteps));
	TestEqual(TEXT("높이 0은 한 구간으로 검사"), ZeroArcSteps, 1);
	auto* ZeroArcShot = LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon,
		FVector(0, 0, 100), FVector(1000, 0, 100), ZeroArc);
	if (!TestNotNull(TEXT("높이 0의 대포도 발사 가능"), ZeroArcShot)) return false;
	ZeroArcShot->Destroy();
	for (const bool bTinyRadius : {false, true})
	{
		FLSCastleWeaponSettings Settings = CoverSettings();
		if (bTinyRadius) Settings.ShellRadius = UE_SMALL_NUMBER;
		else Settings.ArcHeight = 1.e30f;
		TestFalse(TEXT("과도한 분할 비용은 에셋 수치 검증에서 거부"), ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Cannon, Settings));
		int32 Steps = 7;
		TestFalse(TEXT("발사와 같은 순수 분할 기준에서 거부"), ULSCastleWeaponData::TryGetCannonArcSteps(Settings, Steps));
		TestEqual(TEXT("거부한 분할 계산은 출력을 보존"), Steps, 7);
		auto* InvalidDefinition = NewObject<ULSCastleWeaponData>(World);
		InvalidDefinition->Weapon = ELSCastleWeapon::Cannon;
		InvalidDefinition->Levels = {Settings};
		TestFalse(TEXT("동일 수치를 담은 DataAsset 정의도 거부"), InvalidDefinition->IsDefinitionValid());
		TestNull(TEXT("곡선을 한 현으로 바꾸지 않고 과도한 분할 비용을 발사 전에 거부"),
			LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon, FVector(0, 0, 100), FVector(1000, 0, 100), Settings));
	}
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleCoreImpactTest,
	"Landship.Castle.Cover.CannonPhysicalCoreImpactUsesBuildingBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleCoreImpactTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = MakeCoverTarget(World, FVector(-500, 0, 400), ELSFaction::Ally);
	const FTransform CoreTransform(FVector(1200, 0, 400));
	auto* Core = World->SpawnActorDeferred<ALSOutpost>(ALSOutpost::StaticClass(), CoreTransform);
	UDataTable* Table = NewObject<UDataTable>(Core);
	Table->RowStruct = FLSOutpostStatRow::StaticStruct();
	FLSOutpostStatRow Stats;
	Stats.Health = 1000;
	Stats.AttackInterval = 1;
	Stats.AttackRange = 100;
	Table->AddRow(TEXT("CoverCore"), Stats);
	auto* RowProperty = FindFProperty<FStructProperty>(ALSOutpost::StaticClass(), TEXT("StatsRow"));
	auto* Row = RowProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Core);
	Row->DataTable = Table;
	Row->RowName = TEXT("CoverCore");
	Core->FinishSpawning(CoreTransform);
	auto* Body = CastChecked<UBoxComponent>(Core->GetRootComponent());
	Body->SetBoxExtent(FVector(600, 600, 400));
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	auto* Unit = MakeCoverTarget(World, FVector(349, 80, 400));
	CastChecked<USphereComponent>(Unit->GetRootComponent())->SetSphereRadius(80);
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(320, -30, 500));
	Castle->SetInitialFaction(ELSFaction::Enemy);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Core->Orders->SetComponentTickEnabled(false);
	Castle->Orders->SetComponentTickEnabled(false);
	FLSHealthSnapshot Before;
	if (!TestTrue(TEXT("실제 거점의 typed 수치로 체력 준비"), Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Before))) return false;
	FLSHealthSnapshot CastleBefore;
	if (!TestTrue(TEXT("실제 성의 typed 수치로 체력 준비"), Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(CastleBefore))) return false;
	FLSCastleWeaponSettings Settings = CoverSettings();
	Settings.ArcHeight = 0;
	Settings.Radius = 250;
	auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon,
		FVector(0, 0, 400), FVector(1200, 0, 400), Settings);
	if (!TestNotNull(TEXT("실제 Core 상자 옆면으로 발사"), Shot)) return false;
	Shot->Tick(10);
	FLSHealthSnapshot After;
	Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(After);
	FLSHealthSnapshot CastleAfter;
	Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(CastleAfter);
	TestTrue(TEXT("목적지 중심보다 먼저 Core 실제 옆면에서 충돌"), Shot->GetActorLocation().X < 650);
	TestEqual(TEXT("중심 600cm 밖의 접촉도 250cm 반경으로 Core 외곽에 피해"), After.CurrentHealth, Before.CurrentHealth - 20);
	TestEqual(TEXT("넓은 병력 몸체도 기존 중심이 반경 밖이면 제외"), Unit->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Building 채널인 실제 성 Pawn도 기존 중심 판정 유지"), CastleAfter.CurrentHealth, CastleBefore.CurrentHealth);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleUnsupportedBuildingTest,
	"Landship.Castle.Cover.CannonUnsupportedBuildingKeepsCenterFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleUnsupportedBuildingTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = MakeCoverTarget(World, FVector(-500, -500, 100), ELSFaction::Ally);
	auto* Unsupported = MakeCoverTarget(World, FVector(251, 0, 100));
	CastChecked<UPrimitiveComponent>(Unsupported->GetRootComponent())->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	auto* Capsule = NewObject<UCapsuleComponent>(Unsupported);
	Unsupported->AddInstanceComponent(Capsule);
	Unsupported->SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(80, 120);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(LSCollisionChannels::Building);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Capsule->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	Capsule->RegisterComponent();
	Unsupported->SetActorLocation(FVector(251, 0, 100));
	auto* Supported = MakeCoverTarget(World, FVector(0, 251, 100));
	auto* Sphere = CastChecked<USphereComponent>(Supported->GetRootComponent());
	Sphere->SetSphereRadius(80);
	Sphere->SetCollisionObjectType(LSCollisionChannels::Building);
	Sphere->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	FLSCastleWeaponSettings Settings = CoverSettings();
	Settings.ArcHeight = 0;
	Settings.Radius = 250;
	auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Cannon, FVector(0, 0, 100), FVector(0, 0, 100), Settings);
	if (!TestNotNull(TEXT("건물 형상별 범위 검사 발사"), Shot)) return false;
	Shot->Tick(1);
	TestEqual(TEXT("지원하지 않는 정지 건물 캡슐은 기존 중심 판정"), Unsupported->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("지원하는 정지 건물 구는 실제 외곽 판정"), Supported->GetCurrentHealth(), 80.f);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleShotgunCoverPolicyTest,
	"Landship.Castle.Cover.ShotgunWallBodyOrderFriendlySkipAndRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleShotgunCoverPolicyTest::RunTest(const FString& Parameters)
{
	auto* Policy = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool OriginalFriendlyFire = Policy->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Policy->bAllowFriendlyFire = OriginalFriendlyFire; };
	for (const bool bFriendlyFire : {false, true})
	for (const bool bWallFirst : {false, true})
	{
		Policy->bAllowFriendlyFire = bFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCoverTarget(World, FVector(-100, 0, 100), ELSFaction::Ally);
		auto* Friendly = MakeCoverTarget(World, FVector(150, 0, 100), ELSFaction::Ally);
		CastChecked<UPrimitiveComponent>(Friendly->GetRootComponent())->SetCollisionResponseToChannel(
			LSCollisionChannels::ObstacleProjectile, ECR_Block);
		auto* Enemy = MakeCoverTarget(World, FVector(400, 0, 100));
		MakeCoverWall(World, FVector(bWallFirst ? 250 : 500, 0, 100), FVector(5, 100, 100));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Shotgun,
			FVector(0, 0, 100), FVector(600, 0, 100), CoverSettings());
		if (!TestNotNull(TEXT("일반 지면 산탄 발사"), Shot)) return false;
		Shot->Tick(1);
		TestEqual(TEXT("아군 몸체 통과는 기존 오폭 정책"), Friendly->GetCurrentHealth(), bFriendlyFire ? 80.f : 100.f);
		TestEqual(TEXT("아군 통과 뒤 벽과 적 중 실제 먼저 만난 몸체에서 종료"), Enemy->GetCurrentHealth(), !bFriendlyFire && !bWallFirst ? 80.f : 100.f);
		TestTrue(TEXT("산탄 종료"), Shot->IsActorBeingDestroyed());
	}
	for (const float Radius : {2.f, 8.f})
	{
		Policy->bAllowFriendlyFire = false;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCoverTarget(World, FVector(-100, 0, 100), ELSFaction::Ally);
		auto* Enemy = MakeCoverTarget(World, FVector(400, 15, 100));
		MakeCoverWall(World, FVector(200, 0, 100), FVector(5, 10, 50));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSCastleWeaponSettings Settings = CoverSettings();
		Settings.PelletRadius = Radius;
		auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Shotgun,
			FVector(0, 15, 100), FVector(600, 15, 100), Settings);
		if (!TestNotNull(TEXT("실제 탄 반경으로 산탄 발사"), Shot)) return false;
		Shot->Tick(1);
		TestEqual(TEXT("작은 탄은 벽을 스치지 않고 큰 탄은 실제 구체 외곽에서 막힘"), Enemy->GetCurrentHealth(), Radius == 2 ? 80.f : 100.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastlePelletCoverIndependenceTest,
	"Landship.Castle.Cover.ShotgunPelletsStopIndependentlyInBothAimModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastlePelletCoverIndependenceTest::RunTest(const FString& Parameters)
{
	for (const bool bFirstBlocking : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCoverTarget(World, FVector(-100, 0, 100), ELSFaction::Ally);
		TArray<ALSBasicAttackTestActor*> Targets;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto* Target = MakeCoverTarget(World, FVector(0, 0, 100) +
				FVector::ForwardVector.RotateAngleAxis(-20.f + Index * 20.f, FVector::UpVector) * 400);
			CastChecked<UPrimitiveComponent>(Target->GetRootComponent())->SetCollisionResponseToChannel(
				LSCollisionChannels::ObstacleProjectile, ECR_Block);
			Targets.Add(Target);
		}
		MakeCoverWall(World, FVector(200, 0, 100), FVector(1, 8, 50));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSCastleWeaponSettings Settings = CoverSettings();
		Settings.Damage = 30;
		Settings.PelletCount = 3;
		Settings.ConeAngle = 40;
		auto* Shot = LaunchCoverShot(World, Source, ELSCastleWeapon::Shotgun,
			FVector(0, 0, 100), FVector(600, 0, 100), Settings, bFirstBlocking);
		if (!TestNotNull(TEXT("세 탄환을 각 조준 모드로 발사"), Shot)) return false;
		Shot->Tick(1);
		TestEqual(TEXT("왼쪽 탄은 기존 Damage/PelletCount 피해"), Targets[0]->GetCurrentHealth(), 90.f);
		TestEqual(TEXT("중앙 탄만 물리 벽에서 종료"), Targets[1]->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("오른쪽 탄은 독립적으로 계속 이동"), Targets[2]->GetCurrentHealth(), 90.f);
		TestTrue(TEXT("모든 탄이 종료되면 묶음 제거"), Shot->IsActorBeingDestroyed());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

#endif
