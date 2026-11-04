#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Controls/LSOrderComponent.h"
#include "AIController.h"
#include "AssetCompilingManager.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSWallFiringPositionTest, "Landship.Controls.WallFiringPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSWallFiringPositionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto AddBox = [&](const FVector& Location, const FVector& Extent, ECollisionChannel Channel)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Body = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Body);
		Actor->AddInstanceComponent(Body);
		Body->SetBoxExtent(Extent);
		Body->SetMobility(EComponentMobility::Static);
		Body->SetCollisionObjectType(Channel);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Actor->SetActorLocation(Location);
		Body->RegisterComponent();
		return Actor;
	};
	AddBox(FVector(0, 0, -50), FVector(1800, 1800, 50), LSCollisionChannels::Ground);
	AddBox(FVector(-300, 0, 150), FVector(30, 180, 150), ECC_WorldStatic);
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(1900, 1900, 600));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();
	const FTransform SourceTransform(FVector(-600, 0, 90));
	ALSTroopUnit* Source = World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), SourceTransform);
	Source->SetInitialTroopType(ELSTroopType::Archer);
	Source->FinishSpawning(SourceTransform);
	const FTransform TargetTransform(FVector(0, 0, 90));
	ALSTroopUnit* Target = World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), TargetTransform);
	Target->SetInitialFaction(ELSFaction::Enemy);
	Target->FinishSpawning(TargetTransform);
	ULSBattlefieldNavigationSystem* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::GameMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Nav->Build();
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Source->Orders->SetComponentTickEnabled(false);
	Target->Orders->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->BasicAttack->EndBattleActions();
	FLSBasicAttackSettings Settings = Source->BasicAttack->GetAttackSettings();
	Settings.Range = 700;
	Settings.Interval = 10;
	Settings.Damage = 1;
	if (!TestTrue(TEXT("Archer attack configured"), Source->BasicAttack->ConfigureAttack(Settings))) return false;
	TestTrue(TEXT("Target is already inside unchanged physical range"), Source->Orders->IsInAttackRange(Target));
	TestFalse(TEXT("Non-Pawn wall occludes current firing location"),
		Source->BasicAttack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	FVector FiringGoal;
	if (!TestTrue(TEXT("Bounded approach search finds reachable firing position"),
		Source->Orders->ResolveAttackDestination(Target, FiringGoal, false))) return false;
	double CenterHeight = 0;
	if (!TestTrue(TEXT("Candidate preserves actor-to-navigation height"),
		Source->Orders->GetNavigationOriginHeight(CenterHeight))) return false;
	const FVector FiringCenter = FiringGoal + FVector(0, 0, CenterHeight);
	TestTrue(TEXT("Projected candidate has a clear configured-radius shot"),
		Source->BasicAttack->HasClearAttackPathFromLocation(FiringCenter, Target));
	TestTrue(TEXT("Clear candidate remains inside actual attack range"),
		ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Source, Target, FiringCenter) <= Settings.Range);
	FLSOrder Attack;
	Attack.Kind = ELSOrderKind::AttackTarget;
	Attack.Target = Target;
	TestTrue(TEXT("In-range occluded AttackTarget accepts repositioning"), Source->Orders->SubmitOrder(Attack));
	TestTrue(TEXT("Occluded archer approaches instead of standing and firing into wall"),
		Source->Orders->bAttackApproaching && Source->Orders->IsMoving());
	TestNull(TEXT("Repositioning stops the blocked attack"), Source->BasicAttack->GetAttackTarget());
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("Hold accepted"), Source->Orders->SubmitOrder(Hold));
	Source->Orders->UpdateOrders(0);
	TestEqual(TEXT("Occluded Hold remains Hold"), Source->Orders->GetOrderKind(), ELSOrderKind::Hold);
	TestFalse(TEXT("Occluded Hold never repositions"), Source->Orders->IsMoving());
	TestNull(TEXT("Occluded Hold stops attack"), Source->BasicAttack->GetAttackTarget());
	FLSOrder AttackMove;
	AttackMove.Kind = ELSOrderKind::AttackMove;
	AttackMove.Destination = FVector(-1100, 700, 0);
	TestTrue(TEXT("AttackMove accepted"), Source->Orders->SubmitOrder(AttackMove));
	TestTrue(TEXT("AttackMove also seeks a clear firing position while target is in range"),
		Source->Orders->bAttackApproaching && Source->Orders->IsMoving());
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif
