#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/LSCastleProjectile.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationOctree.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

namespace
{
int32 CountCachedNavigationComponents(const UNavigationSystemV1* Navigation, const AActor* Owner)
{
	int32 Count = 0;
	if (const FNavigationOctree* Octree = Navigation->GetNavOctree())
	{
		Octree->FindAllElements([Owner, &Count](const FNavigationOctreeElement& Element)
		{
			const UActorComponent* Component = Cast<UActorComponent>(Element.GetSourceElement()->GetWeakUObject().Get());
			if (Component && Component->GetOwner() == Owner) ++Count;
		});
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleProjectileNavigationTest,
	"Landship.Castle.Projectiles.VisualMeshesNeverEnterNavigationOctree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleProjectileNavigationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Editor)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	auto* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(4000, 4000, 2000));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();
	auto* Navigation = NewObject<UNavigationSystemV1>(World);
	World->SetNavigationSystem(Navigation);
	// 실제 초기화가 이 볼륨을 발견해 내비게이션 데이터와 옥트리를 생성한다. 경로 타일 빌드는 요청하지 않는다.
	Navigation->InitializeForWorld(*World, FNavigationSystemRunMode::EditorMode);
	if (!TestNotNull(TEXT("독립 월드에 실제 내비게이션 옥트리 구성"), Navigation->GetNavOctree())) return false;
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("실제 탄환과 같은 구 메시"), Sphere)) return false;

	AActor* Control = World->SpawnActor<AActor>();
	auto* ControlMesh = NewObject<UStaticMeshComponent>(Control);
	Control->AddInstanceComponent(ControlMesh);
	Control->SetRootComponent(ControlMesh);
	ControlMesh->SetStaticMesh(Sphere);
	Navigation->ProcessPendingOctreeUpdates();
	if (!TestTrue(TEXT("양성 대조군: SetStaticMesh가 미등록 컴포넌트도 옥트리에 등록"),
		CountCachedNavigationComponents(Navigation, Control) > 0)) return false;
	ControlMesh->RegisterComponent();
	Navigation->ProcessPendingOctreeUpdates();
	if (!TestTrue(TEXT("양성 대조군: 컴포넌트 등록 후에도 실제 옥트리 요소 유지"),
		CountCachedNavigationComponents(Navigation, Control) > 0)) return false;

	auto* Source = LSCastleTestFixture::Spawn(World, FVector(0, 0, 500), FRotator::ZeroRotator);
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun})
	{
		FLSCastleWeaponSettings Settings;
		Settings.Range = 3000;
		const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 650));
		auto* Projectile = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(),
			Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("실제 성 투사체 생성"), Projectile)) return false;
		if (!TestTrue(TEXT("Launch가 대포 또는 산탄의 실제 표시 메시 생성"), Projectile->Launch(Weapon, Settings,
			ULSHealthComponent::MakeDamageRequest(Settings.Damage, Source, nullptr), FVector(3000, 0, 0)))) return false;
		TInlineComponentArray<UStaticMeshComponent*> Bullets(Projectile);
		TestEqual(TEXT("실제 탄환 컴포넌트 수 확인"), Bullets.Num(),
			Weapon == ELSCastleWeapon::Cannon ? 1 : Settings.PelletCount);
		Navigation->ProcessPendingOctreeUpdates();
		TestEqual(TEXT("Launch의 메시 생성 직후 투사체 캐시 요소 없음"),
			CountCachedNavigationComponents(Navigation, Projectile), 0);
		Projectile->FinishSpawning(Transform);
		Navigation->ProcessPendingOctreeUpdates();
		TestEqual(TEXT("생성 완료 후에도 투사체 캐시 요소 없음"),
			CountCachedNavigationComponents(Navigation, Projectile), 0);
		for (float Step : {.1f, .2f, .3f})
		{
			Projectile->Tick(Step);
			Navigation->ProcessPendingOctreeUpdates();
			TestFalse(TEXT("검사 시점에 실제 투사체가 비행 중"), Projectile->IsActorBeingDestroyed());
			TestEqual(TEXT("비행 후에도 투사체 캐시 요소 없음"),
				CountCachedNavigationComponents(Navigation, Projectile), 0);
			TestTrue(TEXT("비행 검사 중에도 양성 대조군이 옥트리에 존재"),
				CountCachedNavigationComponents(Navigation, Control) > 0);
		}
		Projectile->Destroy();
	}
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
