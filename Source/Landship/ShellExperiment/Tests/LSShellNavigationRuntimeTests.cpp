#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellNavigationBodyComponent.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSOutpost.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Maps/LSBattlefieldSettings.h"
#include "AIController.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformProcess.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace
{
struct FLSShellNativePathObserver
{
	TArray<FNavPathSharedPtr> Paths;
	TArray<FDelegateHandle> Handles;
	int32 InvalidatedCount = 0;
	int32 Events[8] {};
	TArray<FString> EventTrace;
	~FLSShellNativePathObserver()
	{
		for (int32 Index = 0; Index < Paths.Num(); ++Index) Paths[Index]->RemoveObserver(Handles[Index]);
	}
	void Track(const FNavPathSharedPtr& Path)
	{
		if (!Path.IsValid() || Paths.Contains(Path)) return;
		Paths.Add(Path);
		Handles.Add(Path->AddObserver(FNavigationPath::FPathObserverDelegate::FDelegate::CreateLambda(
			[this](FNavigationPath* ChangedPath, ENavPathEvent::Type Event)
			{
				const int32 Index = static_cast<int32>(Event);
				if (Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Events))) ++Events[Index];
				if (Event == ENavPathEvent::Invalidated) ++InvalidatedCount;
				if (EventTrace.Num() < 24) EventTrace.Add(FString::Printf(TEXT("Event=%d Path=%p Ready=%d UpToDate=%d Ignore=%d AutoRepath=%d Stamp=%.3f"),
					Index, static_cast<const void*>(ChangedPath), ChangedPath->IsReady(), ChangedPath->IsUpToDate(),
					ChangedPath->GetIgnoreInvalidation(), ChangedPath->WillRecalculateOnInvalidation(), ChangedPath->GetTimeStamp()));
			})));
	}
	FString Describe() const
	{
		FString Text = FString::Printf(TEXT("Tracked=%d Cleared=%d New=%d GoalUpdate=%d NavUpdate=%d Invalidated=%d RepathFailed=%d Meta=%d Custom=%d"),
			Paths.Num(), Events[0], Events[1], Events[2], Events[3], Events[4], Events[5], Events[6], Events[7]);
		for (const FString& Event : EventTrace) Text += TEXT(" | ") + Event;
		return Text;
	}
};

TArray<NavNodeRef> RuntimeCorridorRefs(const FNavPathSharedPtr& Path)
{
	const FNavMeshPath* Corridor = Path.IsValid() ? Path->CastPath<FNavMeshPath>() : nullptr;
	return Corridor ? Corridor->PathCorridor : TArray<NavNodeRef>();
}

FString RuntimeCorridorState(const FNavPathSharedPtr& Path, const ARecastNavMesh* Mesh, const TArray<NavNodeRef>& CapturedRefs)
{
	FString BeforeText, CurrentText;
	int32 CapturedStillValid = 0, CurrentValid = 0;
	for (NavNodeRef Ref : CapturedRefs)
	{
		CapturedStillValid += Mesh->IsNodeRefValid(Ref) ? 1 : 0;
		if (!BeforeText.IsEmpty()) BeforeText += TEXT(",");
		BeforeText += FString::Printf(TEXT("%llu"), static_cast<uint64>(Ref));
	}
	const TArray<NavNodeRef> Current = RuntimeCorridorRefs(Path);
	for (NavNodeRef Ref : Current)
	{
		CurrentValid += Mesh->IsNodeRefValid(Ref) ? 1 : 0;
		if (!CurrentText.IsEmpty()) CurrentText += TEXT(",");
		CurrentText += FString::Printf(TEXT("%llu"), static_cast<uint64>(Ref));
	}
	return FString::Printf(TEXT("Path=%p Ready=%d UpToDate=%d Valid=%d Ignore=%d AutoRepath=%d CapturedValid=%d/%d CurrentValid=%d/%d SameCorridor=%d Captured=[%s] Current=[%s]"),
		static_cast<const void*>(Path.Get()), Path.IsValid() && Path->IsReady(), Path.IsValid() && Path->IsUpToDate(),
		Path.IsValid() && Path->IsValid(), Path.IsValid() && Path->GetIgnoreInvalidation(), Path.IsValid() && Path->WillRecalculateOnInvalidation(),
		CapturedStillValid, CapturedRefs.Num(), CurrentValid, Current.Num(), Current == CapturedRefs, *BeforeText, *CurrentText);
}

TArray<FNavTileRef> ReadRuntimeTileRefs(const ARecastNavMesh* Mesh, int32 X, int32 Y)
{
	TArray<FNavTileRef> Refs;
	Mesh->GetNavMeshTilesAt(X, Y, Refs);
	Refs.Sort([](FNavTileRef Left, FNavTileRef Right) { return static_cast<uint64>(Left) < static_cast<uint64>(Right); });
	return Refs;
}

FString RuntimeTileRefsText(const TArray<FNavTileRef>& Refs)
{
	FString Text;
	for (FNavTileRef Ref : Refs)
	{
		if (!Text.IsEmpty()) Text += TEXT(",");
		Text += FString::Printf(TEXT("%llu"), static_cast<uint64>(Ref));
	}
	return Text;
}

bool RuntimeCorridorContainsTile(const FNavPathSharedPtr& Path, const ARecastNavMesh* Mesh, int32 X, int32 Y)
{
	const FNavMeshPath* Corridor = Path.IsValid() ? Path->CastPath<FNavMeshPath>() : nullptr;
	if (!Corridor) return false;
	for (FNavTileRef Tile : ReadRuntimeTileRefs(Mesh, X, Y))
	{
		TArray<FNavPoly> Polys;
		if (!Mesh->GetPolysInTile(Tile, Polys)) continue;
		for (const FNavPoly& Poly : Polys) if (Corridor->PathCorridor.Contains(Poly.Ref)) return true;
	}
	return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellNativeSameTileRuntimeTest,
	"Landship.ShellExperiment.Navigation.NativeSameTileSettleWakeRemovalReplansMovingAssault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellNativeSameTileRuntimeTest::RunTest(const FString& Parameters)
{
	auto* Policy = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bPreviousFriendlyFire = Policy->bAllowFriendlyFire, bPreviousSelfDamage = Policy->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Policy->bAllowFriendlyFire = bPreviousFriendlyFire;
		Policy->bAllowSelfDamage = bPreviousSelfDamage;
	};
	Policy->bAllowFriendlyFire = Policy->bAllowSelfDamage = true;
	FEnumProperty* RuntimeProperty = FindFProperty<FEnumProperty>(ANavigationData::StaticClass(), TEXT("RuntimeGeneration"));
	if (!TestNotNull(TEXT("Installed runtime-generation property"), RuntimeProperty)) return false;
	auto* DefaultMesh = GetMutableDefault<ARecastNavMesh>();
	void* RuntimeAddress = RuntimeProperty->ContainerPtrToValuePtr<void>(DefaultMesh);
	const uint64 PreviousRuntimeMode = RuntimeProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(RuntimeAddress);
	ON_SCOPE_EXIT { RuntimeProperty->GetUnderlyingProperty()->SetIntPropertyValue(RuntimeAddress, PreviousRuntimeMode); };
	RuntimeProperty->GetUnderlyingProperty()->SetIntPropertyValue(RuntimeAddress, static_cast<uint64>(ERuntimeGenerationType::Dynamic));

	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("Isolated real game world"), Fixture.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = Fixture.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Floor = World->SpawnActor<AActor>();
	auto* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody);
	Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetBoxExtent(FVector(4000, 2500, 50));
	FloorBody->SetMobility(EComponentMobility::Static);
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetWorldLocation(FVector(0, 0, -50));
	auto* FloorMaterial = NewObject<UPhysicalMaterial>(Floor);
	FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
	FloorMaterial->Restitution = .3f;
	FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
	FloorBody->SetPhysMaterialOverride(FloorMaterial);
	FloorBody->RegisterComponent();
	if (!TestTrue(TEXT("Generic floor root really has its top at ground zero"),
		FMath::IsNearlyEqual(FloorBody->GetComponentLocation().Z, -50., .01) && FMath::IsNearlyEqual(FloorBody->Bounds.GetBox().Max.Z, 0., .01))) return false;
	auto* Settings = World->SpawnActor<ALSBattlefieldSettings>();
	Settings->BoundsMin = FVector2D(-3900, -2400);
	Settings->BoundsMax = FVector2D(3900, 2400);
	Settings->GroundHeight = 0;
	Settings->TerrainActors = {Floor};
	auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	auto* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(4100, 2600, 800));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();
	// 네이티브 성의 실제 캡슐을 지면에 놓는다. 다른 Blueprint의 중심 높이를 복사하면 근접 사거리 밖으로 뜬다.
	const float CastleHalfHeight = GetDefault<ALSCastle>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-2600, -700, CastleHalfHeight));
	if (!TestNotNull(TEXT("Production castle"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(2600, 1300, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Production core actor"), Core)) return false;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::GameMode);
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
#endif
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Nav->Build();
	if (!TestTrue(TEXT("Navigation owns the actual Troops profile"),
		!Nav->GetSupportedAgents().IsEmpty() && Nav->GetSupportedAgents()[0].Name == FName(TEXT("Troops")))) return false;
	const FNavDataConfig& TroopsConfig = Nav->GetSupportedAgents()[0];
	auto* Mesh = Cast<ARecastNavMesh>(Nav->GetNavDataForProps(TroopsConfig));
	if (!TestNotNull(TEXT("Actual Troops Recast data"), Mesh)) return false;
	if (!TestEqual(TEXT("Geometry updates use real Dynamic generation"), Mesh->GetRuntimeGenerationMode(), ERuntimeGenerationType::Dynamic)) return false;
	Mesh->EnsureBuildCompletion();
	TArray<FNavTileRef> Tiles;
	Mesh->GetAllNavMeshTiles(Tiles);
	FBox SelectedTile(ForceInit);
	int32 TileX = 0, TileY = 0;
	double BestDistance = TNumericLimits<double>::Max();
	for (FNavTileRef Tile : Tiles)
	{
		const FBox Box = Mesh->GetNavMeshTileBounds(Tile);
		int32 X, Y, Layer;
		if (!Box.IsValid || !Mesh->GetNavMeshTileXY(Tile, X, Y, Layer) || Box.GetSize().X < 850 || Box.GetSize().Y < 850) continue;
		if (FMath::Abs(Box.GetCenter().X) > 1100 || FMath::Abs(Box.GetCenter().Y) > 1100) continue;
		const double Distance = Box.GetCenter().SizeSquared2D();
		if (Distance < BestDistance) { BestDistance = Distance; SelectedTile = Box; TileX = X; TileY = Y; }
	}
	if (!TestTrue(TEXT("Instrument finds an actual populated central tile with lateral room"), SelectedTile.IsValid != 0)) return false;
	const float HalfHeight = GetDefault<ALSShellExperimentEnemy>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform EnemyTransform(FVector(SelectedTile.Max.X - 180, SelectedTile.Min.Y + 200, HalfHeight));
	auto* Enemy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), EnemyTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Production Assault actor"), Enemy)) return false;
	Enemy->EnemyKind = ELSShellEnemyKind::Assault;
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	if (!TestNotNull(TEXT("Production Assault stats"), Enemy->StatsTable.Get())) return false;
	Enemy->FinishSpawning(EnemyTransform);
	if (!TestTrue(TEXT("BeginPlay through actual Shell GameMode"), Fixture.BeginPlayInTestWorld())) return false;
	const auto* Mode = World->GetAuthGameMode<ALSShellExperimentGameMode>();
	if (!TestTrue(TEXT("Shell battle is valid and active"), Mode && Mode->IsConfigurationValid() && Coordinator->IsBattleActive())) return false;
	FLSUnitStatRow ProductionStats;
	if (!TestTrue(TEXT("Assault uses initialized production stats and living common Health"),
		Enemy->TryGetStats(ProductionStats) && ULSCombatTargetLibrary::IsCombatAlive(Enemy) && Enemy->Behavior->IsComponentTickEnabled())) return false;
	Castle->Orders->SetComponentTickEnabled(false);
	auto* AI = Cast<AAIController>(Enemy->GetController());
	auto* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!TestNotNull(TEXT("Production Assault path follower"), Following)) return false;
	if (!TestEqual(TEXT("Assault selects this actual Troops NavData"), Nav->GetNavDataForProps(Enemy->GetNavAgentPropertiesRef(), Enemy->GetNavAgentLocation()),
		static_cast<ANavigationData*>(Mesh))) return false;
	auto ReportRouteSetup = [&](const TCHAR* Stage)
	{
		const FNavPathSharedPtr Path = Following->GetPath();
		FNavLocation StartPoint, CastlePoint;
		const bool bStartProjects = Nav->ProjectPointToNavigation(Enemy->GetNavAgentLocation(), StartPoint, FVector(150, 150, 300), Mesh);
		const bool bCastleProjects = Nav->ProjectPointToNavigation(Castle->GetNavAgentLocation(), CastlePoint, FVector(150, 150, 300), Mesh);
		FVector BesideCastle = Castle->GetActorLocation();
		BesideCastle.Z = Enemy->GetActorLocation().Z;
		const double VerticalOnlyDistance = ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Enemy, Castle, BesideCastle);
		const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
		AddInfo(FString::Printf(TEXT("NativeRouteSetup Stage=%s Time=%.3f Tile=(%d,%d) TileBounds=%s Floor=%s FloorTop=%.3f Enemy=%s NavAgent=%s CapsuleHalf=%.3f Castle=%s CastleHalf=%.3f CastleFoot=%s VerticalOnlyAttackDistance=%.3f AttackRange=%.3f BehaviorState=%d BehaviorTick=%d StopReason=%s Target=%s MoveMode=%d MovementTick=%d Velocity=%s FollowingStatus=%d Request=%u PathValid=%d Partial=%d PathData=%s StartProjects=%d StartProjection=%s CastleProjects=%d CastleProjection=%s"),
			Stage, World->GetTimeSeconds(), TileX, TileY, *SelectedTile.ToString(), *FloorBody->GetComponentLocation().ToCompactString(), FloorBody->Bounds.GetBox().Max.Z,
			*Enemy->GetActorLocation().ToCompactString(), *Enemy->GetNavAgentLocation().ToCompactString(), Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			*Castle->GetActorLocation().ToCompactString(), Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), *Castle->GetNavAgentLocation().ToCompactString(),
			VerticalOnlyDistance, Enemy->BasicAttack->GetAttackSettings().Range, static_cast<int32>(Enemy->Behavior->GetState()), Enemy->Behavior->IsComponentTickEnabled(),
			*Enemy->Behavior->GetLastStopReason(), *GetNameSafe(Enemy->Behavior->GetTarget()), static_cast<int32>(Movement->MovementMode), Movement->IsComponentTickEnabled(),
			*Enemy->GetVelocity().ToCompactString(), static_cast<int32>(Following->GetStatus()), static_cast<uint32>(Following->GetCurrentRequestId()),
			Path.IsValid() && Path->IsValid(), Path.IsValid() && Path->IsPartial(), *GetNameSafe(Path.IsValid() ? Path->GetNavigationDataUsed() : nullptr),
			bStartProjects, *StartPoint.Location.ToCompactString(), bCastleProjects, *CastlePoint.Location.ToCompactString()));
	};
	ReportRouteSetup(TEXT("before route observation"));
	FVector BesideCastle = Castle->GetActorLocation();
	BesideCastle.Z = Enemy->GetActorLocation().Z;
	if (!TestTrue(TEXT("Fixture places native castle within vertical reach of grounded production melee"),
		ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Enemy, Castle, BesideCastle) <= Enemy->BasicAttack->GetAttackSettings().Range)) return false;
	FLSShellNativePathObserver Observer;
	auto Tick = [&]()
	{
		Observer.Track(Following->GetPath());
		++GFrameCounter;
		World->Tick(LEVELTICK_All, .01f);
		Observer.Track(Following->GetPath());
		// 정상 TickAsyncBuild가 완료 타일 목록을 받아 경로를 무효화해야 한다. EnsureBuildCompletion은 그 목록을 버린다.
		FPlatformProcess::SleepNoStats(0);
		Observer.Track(Following->GetPath());
	};
	auto WaitFor = [&](TFunctionRef<bool()> Predicate)
	{
		for (int32 Step = 0; Step < 800; ++Step) { Tick(); if (Predicate()) return true; }
		return false;
	};
	auto HasMovingCompletePath = [&]()
	{
		const FNavPathSharedPtr Path = Following->GetPath();
		return Following->GetStatus() == EPathFollowingStatus::Moving && Path.IsValid() && Path->IsValid() &&
			!Path->IsPartial() && Path->GetNavigationDataUsed() == Mesh && Enemy->GetVelocity().Size2D() > 1;
	};
	const bool bInitialRouteReady = WaitFor(HasMovingCompletePath);
	ReportRouteSetup(bInitialRouteReady ? TEXT("native route ready") : TEXT("native route timeout"));
	if (!TestTrue(TEXT("Instrument reaches a real complete moving Assault route"), bInitialRouteReady)) return false;
	int32 EnemyTileX, EnemyTileY;
	Mesh->GetNavMeshTileXY(Enemy->GetNavAgentLocation(), EnemyTileX, EnemyTileY);
	if (!TestTrue(TEXT("Initial enemy and native corridor use the selected actual tile"),
		EnemyTileX == TileX && EnemyTileY == TileY && RuntimeCorridorContainsTile(Following->GetPath(), Mesh, TileX, TileY))) return false;
	const TArray<FNavPathPoint>& Points = Following->GetPath()->GetPathPoints();
	const float Radius = GetDefault<ALSShellMetalBall>()->Radius;
	FVector BallPosition, LateralDirection, SelectedRoutePoint;
	bool bPlaced = false;
	for (int32 Index = 1; Index < Points.Num() && !bPlaced; ++Index)
	{
		const FVector Segment = Points[Index].Location - Points[Index - 1].Location;
		if (Segment.Size2D() < 350) continue;
		const FVector Along = Segment.GetSafeNormal2D();
		const FVector OnRoute = Points[Index - 1].Location + Along * 250;
		const FVector Side(-Along.Y, Along.X, 0);
		for (float Sign : {1.f, -1.f})
		{
			FVector Candidate = OnRoute + Side * Sign * 300;
			Candidate.Z = Radius;
			int32 X, Y;
			Mesh->GetNavMeshTileXY(Candidate, X, Y);
			if (X != TileX || Y != TileY || Candidate.X - Radius < SelectedTile.Min.X || Candidate.X + Radius > SelectedTile.Max.X ||
				Candidate.Y - Radius < SelectedTile.Min.Y || Candidate.Y + Radius > SelectedTile.Max.Y) continue;
			BallPosition = Candidate; LateralDirection = Side * Sign; SelectedRoutePoint = OnRoute; bPlaced = true; break;
		}
	}
	if (!TestTrue(TEXT("Real corridor provides a same-tile300cm lateral ball pose with agent clearance"),
		bPlaced && 300 > Mesh->GetConfig().AgentRadius + Radius + 3)) return false;
	const TArray<FNavTileRef> OpenRefs = ReadRuntimeTileRefs(Mesh, TileX, TileY);
	const FNavPathSharedPtr OpenPath = Following->GetPath();
	const TArray<NavNodeRef> OpenCorridor = RuntimeCorridorRefs(OpenPath);
	const FAIRequestID OpenRequest = Following->GetCurrentRequestId();
	const int32 OpenInvalidations = Observer.InvalidatedCount;
	const FVector OpenEnemyPosition = Enemy->GetActorLocation();
	auto* Ball = World->SpawnActor<ALSShellMetalBall>(BallPosition, FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Production physical ball"), Ball)) return false;
	TWeakObjectPtr<ALSShellMetalBall> BallLifetime = Ball;
	int32 BallTileX, BallTileY;
	Mesh->GetNavMeshTileXY(Ball->GetActorLocation(), BallTileX, BallTileY);
	if (!TestTrue(TEXT("Observed actual ball begins300cm beside the real corridor on the enemy's actual tile"),
		BallTileX == EnemyTileX && BallTileY == EnemyTileY && FMath::IsNearlyEqual(
			FVector::Dist2D(Ball->GetActorLocation(), SelectedRoutePoint), 300., .1))) return false;
	if (!TestTrue(TEXT("Production ball starts simulating outside nav"), Ball->Body->IsSimulatingPhysics() && !Ball->Body->CanEverAffectNavigation())) return false;
	const bool bSettled = WaitFor([&]()
	{
		return Ball->NavigationBody->IsNavigationRegistered() && ReadRuntimeTileRefs(Mesh, TileX, TileY) != OpenRefs &&
			Observer.InvalidatedCount > OpenInvalidations && Following->GetCurrentRequestId() != OpenRequest && HasMovingCompletePath() &&
			FVector::Dist2D(OpenEnemyPosition, Enemy->GetActorLocation()) > 10;
	});
	AddInfo(FString::Printf(TEXT("NativeSettle Tile=(%d,%d) Ball=%s RefsBefore=%s RefsAfter=%s Invalidations=%d Enemy=%s V=%s"),
		TileX, TileY, *BallPosition.ToCompactString(), *RuntimeTileRefsText(OpenRefs), *RuntimeTileRefsText(ReadRuntimeTileRefs(Mesh, TileX, TileY)),
		Observer.InvalidatedCount - OpenInvalidations, *Enemy->GetActorLocation().ToCompactString(), *Enemy->GetVelocity().ToCompactString()));
	AddInfo(FString::Printf(TEXT("NativeSettlePaths RequestBefore=%u RequestAfter=%u SameObject=%d Events={%s} Old={%s} Current={%s}"),
		OpenRequest.GetID(), Following->GetCurrentRequestId().GetID(), OpenPath == Following->GetPath(), *Observer.Describe(),
		*RuntimeCorridorState(OpenPath, Mesh, OpenCorridor), *RuntimeCorridorState(Following->GetPath(), Mesh, OpenCorridor)));
	if (!TestTrue(TEXT("Real ball settle rebuilds its actual shared tile, invalidates native path and resumes moving Assault"), bSettled)) return false;
	if (!TestTrue(TEXT("The current native corridor still shares the actual ball tile before wake"),
		RuntimeCorridorContainsTile(Following->GetPath(), Mesh, TileX, TileY))) return false;
	const TArray<FNavTileRef> SettledRefs = ReadRuntimeTileRefs(Mesh, TileX, TileY);
	const FNavPathSharedPtr SettledPath = Following->GetPath();
	const TArray<NavNodeRef> SettledCorridor = RuntimeCorridorRefs(SettledPath);
	const FAIRequestID SettledRequest = Following->GetCurrentRequestId();
	const int32 SettledInvalidations = Observer.InvalidatedCount;
	const FVector BeforeWake = Ball->GetActorLocation(), EnemyBeforeWake = Enemy->GetActorLocation();
	if (!TestTrue(TEXT("Physical wake impulse accepted"), Ball->Reactive->ApplyMomentum(LateralDirection * Ball->Body->GetMass() * 350))) return false;
	const bool bWoke = WaitFor([&]()
	{
		return FVector::Dist2D(BeforeWake, Ball->GetActorLocation()) > 10 && !Ball->NavigationBody->IsNavigationRegistered() &&
			ReadRuntimeTileRefs(Mesh, TileX, TileY) != SettledRefs && Observer.InvalidatedCount > SettledInvalidations &&
			Following->GetCurrentRequestId() != SettledRequest && HasMovingCompletePath() && FVector::Dist2D(EnemyBeforeWake, Enemy->GetActorLocation()) > 10;
	});
	AddInfo(FString::Printf(TEXT("NativeWake Tile=(%d,%d) BallDelta=%.3f RefsBefore=%s RefsAfter=%s Invalidations=%d Enemy=%s V=%s"),
		TileX, TileY, FVector::Dist2D(BeforeWake, Ball->GetActorLocation()), *RuntimeTileRefsText(SettledRefs),
		*RuntimeTileRefsText(ReadRuntimeTileRefs(Mesh, TileX, TileY)), Observer.InvalidatedCount - SettledInvalidations,
		*Enemy->GetActorLocation().ToCompactString(), *Enemy->GetVelocity().ToCompactString()));
	AddInfo(FString::Printf(TEXT("NativeWakePaths RequestBefore=%u RequestAfter=%u SameObject=%d Events={%s} Old={%s} Current={%s}"),
		SettledRequest.GetID(), Following->GetCurrentRequestId().GetID(), SettledPath == Following->GetPath(), *Observer.Describe(),
		*RuntimeCorridorState(SettledPath, Mesh, SettledCorridor), *RuntimeCorridorState(Following->GetPath(), Mesh, SettledCorridor)));
	if (!TestTrue(TEXT("Actual physical motion unregisters geometry, rebuilds shared tile and replans moving enemy"), bWoke)) return false;
	const TArray<FNavTileRef> WokenRefs = ReadRuntimeTileRefs(Mesh, TileX, TileY);
	const int32 WokenInvalidations = Observer.InvalidatedCount;
	// 제거 단계도 등록된 실물에서 검사하려고 실제 선·각운동량의 반대 충격으로 제동한다. 자세·속도를 직접 쓰지 않는다.
	const FVector BrakeImpulse = -Ball->Body->GetPhysicsLinearVelocity() * Ball->Body->GetMass();
	const FQuat BodyRotation = Ball->Body->GetComponentQuat();
	const FVector LocalAngularVelocity = BodyRotation.UnrotateVector(Ball->Body->GetPhysicsAngularVelocityInRadians());
	const FVector AngularBrakeImpulse = -BodyRotation.RotateVector(Ball->Body->GetInertiaTensor() * LocalAngularVelocity);
	Ball->Reactive->ApplyMomentum(BrakeImpulse);
	Ball->Body->AddAngularImpulseInRadians(AngularBrakeImpulse);
	if (!TestTrue(TEXT("Physical braking allows the same real body to settle and register again"),
		WaitFor([&]() { return Ball->NavigationBody->IsNavigationRegistered() && ReadRuntimeTileRefs(Mesh, TileX, TileY) != WokenRefs &&
			Observer.InvalidatedCount > WokenInvalidations && HasMovingCompletePath(); }))) return false;
	int32 RemovalX, RemovalY;
	Mesh->GetNavMeshTileXY(Ball->GetActorLocation(), RemovalX, RemovalY);
	if (!TestTrue(TEXT("Removal remains on the actual corridor tile"), RemovalX == TileX && RemovalY == TileY &&
		RuntimeCorridorContainsTile(Following->GetPath(), Mesh, TileX, TileY))) return false;
	const TArray<FNavTileRef> RemovalRefs = ReadRuntimeTileRefs(Mesh, TileX, TileY);
	const FNavPathSharedPtr RemovalPath = Following->GetPath();
	const TArray<NavNodeRef> RemovalCorridor = RuntimeCorridorRefs(RemovalPath);
	const int32 RemovalInvalidations = Observer.InvalidatedCount;
	const FAIRequestID RemovalRequest = Following->GetCurrentRequestId();
	const FVector EnemyBeforeRemoval = Enemy->GetActorLocation();
	if (!TestTrue(TEXT("Technical removal destroys the actual actor"), Ball->Destroy())) return false;
	const bool bRemoved = WaitFor([&]()
	{
		return !BallLifetime.IsValid() && ReadRuntimeTileRefs(Mesh, TileX, TileY) != RemovalRefs &&
			Observer.InvalidatedCount > RemovalInvalidations && Following->GetCurrentRequestId() != RemovalRequest &&
			HasMovingCompletePath() && FVector::Dist2D(EnemyBeforeRemoval, Enemy->GetActorLocation()) > 10;
	});
	AddInfo(FString::Printf(TEXT("NativeRemove Tile=(%d,%d) RefsBefore=%s RefsAfter=%s Invalidations=%d Enemy=%s V=%s"),
		TileX, TileY, *RuntimeTileRefsText(RemovalRefs), *RuntimeTileRefsText(ReadRuntimeTileRefs(Mesh, TileX, TileY)),
		Observer.InvalidatedCount - RemovalInvalidations, *Enemy->GetActorLocation().ToCompactString(), *Enemy->GetVelocity().ToCompactString()));
	AddInfo(FString::Printf(TEXT("NativeRemovePaths RequestBefore=%u RequestAfter=%u SameObject=%d Events={%s} Old={%s} Current={%s}"),
		RemovalRequest.GetID(), Following->GetCurrentRequestId().GetID(), RemovalPath == Following->GetPath(), *Observer.Describe(),
		*RuntimeCorridorState(RemovalPath, Mesh, RemovalCorridor), *RuntimeCorridorState(Following->GetPath(), Mesh, RemovalCorridor)));
	TestTrue(TEXT("Actual registered-body removal rebuilds shared tile, invalidates route and preserves enemy progress"), bRemoved);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
