#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Troops/LSTroopProductionComponent.h"
#include "AssetCompilingManager.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavModifierComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

namespace
{
AActor* MakeProductionTestBox(UWorld* World, const FVector& Location, const FVector& Extent, bool bNavGeometry)
{
	AActor* Actor = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box);
	Actor->AddInstanceComponent(Box);
	Box->SetBoxExtent(Extent);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCanEverAffectNavigation(bNavGeometry);
	Actor->SetActorLocation(Location);
	Box->RegisterComponent();
	return Actor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopProductionConnectivityTest, "Landship.Troops.ProductionConnectivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopProductionConnectivityTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("Isolated navigation world"), Fixture.CreateTestWorld(EWorldType::Editor))) return false;
	UWorld* World = Fixture.GetTestWorld();
	MakeProductionTestBox(World, FVector(0, 0, -50), FVector(1800, 1800, 50), true);
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(1900, 1900, 600));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();

	TArray<AActor*> Walls;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bVertical = Index < 2;
		const float Sign = Index % 2 ? 1.0f : -1.0f;
		const FVector Location = bVertical ? FVector(Sign * 501, 0, 250) : FVector(0, Sign * 501, 250);
		const FVector Extent = bVertical ? FVector(1, 502, 250) : FVector(502, 1, 250);
		AActor* Wall = MakeProductionTestBox(World, Location, Extent, false);
		// 물리 벽과 같은 얇은 통행 금지 영역으로 안팎을 분리한다. 성은 내비게이션 표면을 파내지 않는다.
		UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(Wall);
		Wall->AddInstanceComponent(Modifier);
		Modifier->FailsafeExtent = Extent;
		Modifier->AreaClass = UNavArea_Null::StaticClass();
		Modifier->RegisterComponent();
		Walls.Add(Wall);
	}
	// 내부의 빈 자리를 막되 NavMesh 연결은 바꾸지 않아, 생산은 반드시 벽 바깥 후보까지 검사한다.
	MakeProductionTestBox(World, FVector(0, 0, 100), FVector(525, 525, 100), false);
	AActor* Producer = World->SpawnActor<AActor>();
	UCapsuleComponent* Body = NewObject<UCapsuleComponent>(Producer);
	Producer->SetRootComponent(Body);
	Producer->AddInstanceComponent(Body);
	Body->InitCapsuleSize(500, 500);
	Body->SetCollisionObjectType(LSCollisionChannels::Unit);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCanEverAffectNavigation(false);
	Producer->SetActorLocation(FVector(0, 0, 500));
	Body->RegisterComponent();
	ULSTroopProductionComponent* Production = NewObject<ULSTroopProductionComponent>(Producer);
	Production->MaxSearchDistance = 1000;

	ULSBattlefieldNavigationSystem* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::EditorMode);
	// 별도 에디터 월드는 자동 빌드의 에셋 대기 잠금을 건다. 동기 테스트에서는 에셋 완료 뒤 이 월드의 잠금만 해제한다.
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	if (!TestFalse(TEXT("Fixture navigation is unlocked before synchronous build"), Nav->IsNavigationBuildingLocked())) return false;
	// Null 영역도 에이전트 반경만큼 확장된다. 얇은 벽 바깥 경계가 50cm 투영 범위에 들어오는 경우를 정밀 셀로 재현한다.
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		It->SetCellSize(ENavigationDataResolution::Default, 5.0f);
	}
	Nav->Build();
	float Radius, HalfHeight;
	ALSTroopUnit::GetCapsuleDimensions(ELSTroopType::Spearman, Radius, HalfHeight);
	FNavAgentProperties Properties = GetDefault<ALSTroopUnit>()->GetNavAgentPropertiesRef();
	Properties.AgentRadius = Radius;
	Properties.AgentHeight = HalfHeight * 2;
	const ANavigationData* Data = Nav->GetNavDataForProps(Properties, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Troop navigation data"), Data)) return false;
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
	AddInfo(FString::Printf(TEXT("Production fixture nav: volume=%s data=%s mesh=%d building=%d"),
		*Bounds->GetComponentsBoundingBox(true).ToString(), *Data->GetBounds().ToString(),
		Recast && Recast->HasValidNavmesh(), Nav->IsNavigationBuildInProgress()));
	FNavLocation Origin, Outside;
	if (!TestTrue(TEXT("Producer projects to troop surface"), Nav->ProjectPointToNavigation(
		FVector::ZeroVector, Origin, FVector(50, 50, 300), Data))) return false;
	if (!TestTrue(TEXT("Free candidate exists beyond thin wall"), Nav->ProjectPointToNavigation(
		FVector(-726, 0, 0), Outside, FVector(50, 50, 300), Data))) return false;
	auto HasCompletePath = [&](const FVector& Start)
	{
		FPathFindingQuery Query(Production, *Data, Start, Outside.Location);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Nav->FindPathSync(Properties, Query);
		return Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid() && !Result.Path->IsPartial();
	};
	FNavLocation OutsideStart;
	if (!TestTrue(TEXT("Second distinct outside point projects"), Nav->ProjectPointToNavigation(
		FVector(-850, 0, 0), OutsideStart, FVector(50, 50, 300), Data))) return false;
	if (!TestTrue(TEXT("Candidate island has a valid nonzero path, which cannot prove producer access"),
		HasCompletePath(OutsideStart.Location))) return false;
	FNavLocation OldRayOrigin;
	if (!TestTrue(TEXT("Old first-ring ray origin projects beyond the thin wall"), Nav->ProjectPointToNavigation(
		FVector(-602, 0, 0), OldRayOrigin, FVector(50, 50, 300), Data))) return false;
	AddInfo(FString::Printf(TEXT("Production fixture points: producer=%s oldRay=%s outside=%s"),
		*Origin.Location.ToString(), *OldRayOrigin.Location.ToString(), *Outside.Location.ToString()));
	if (!TestTrue(TEXT("Old ray origin incorrectly accepts the disconnected outside candidate"),
		HasCompletePath(OldRayOrigin.Location))) return false;
	if (!TestFalse(TEXT("Thin wall disconnects candidate from producer"), HasCompletePath(Origin.Location))) return false;
	FVector SpawnLocation(12345, 12345, 12345);
	TestFalse(TEXT("Disconnected free candidates cannot be production positions"),
		Production->FindSpawnLocation(Producer, ELSTroopType::Spearman, SpawnLocation));
	TestTrue(TEXT("Failed search does not write a spawn location"), SpawnLocation == FVector(12345, 12345, 12345));
	for (int32 Index = 1; Index < Walls.Num(); ++Index) Walls[Index]->Destroy();
	Nav->Build();
	if (!TestTrue(TEXT("An intact rear wall has a complete detour to the far-side candidate"),
		HasCompletePath(Origin.Location))) return false;
	FVector ApproachHit;
	TestTrue(TEXT("Intact rear wall blocks direct troop navigation despite the complete detour"),
		Data->Raycast(Origin.Location, Outside.Location, ApproachHit, Data->GetDefaultQueryFilter(), Production));
	SpawnLocation = FVector(12345, 12345, 12345);
	const bool bFoundBesideWall = Production->FindSpawnLocation(Producer, ELSTroopType::Spearman, SpawnLocation);
	if (!TestTrue(TEXT("Open local sides provide a production position while the rear wall remains intact"), bFoundBesideWall)) return false;
	TestTrue(TEXT("Production uses the available local side rather than crossing the intact rear wall"), SpawnLocation.X > -500);
	TestFalse(TEXT("Actual chosen position has a clear direct troop approach"),
		Data->Raycast(Origin.Location, SpawnLocation - FVector(0, 0, HalfHeight + 4), ApproachHit,
			Data->GetDefaultQueryFilter(), Production));
	AddInfo(FString::Printf(TEXT("Production fixture intact-wall local spawn=%s"), *SpawnLocation.ToString()));
	Walls[0]->Destroy();
	Nav->Build();
	TestTrue(TEXT("Removing walls restores a reachable production position"),
		Production->FindSpawnLocation(Producer, ELSTroopType::Spearman, SpawnLocation));
	return true;
}

#endif
