#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellSiegeController.h"
#include "ShellExperiment/LSShellSupplySchedule.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyDepartureTimingTest,
	"Landship.ShellExperiment.Schedule.FixedDeparturesAndChargedSecondLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyDepartureTimingTest::RunTest(const FString& Parameters)
{
	FLSShellSupplyRoute A;
	FLSShellSupplyRoute B;
	B.FirstDepartureSeconds = 16; B.RepeatSeconds = 36;
	TestEqual(TEXT("A first departure is immediate"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(A, 0), 1);
	TestEqual(TEXT("A second departure is not early"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(A, 23.99), 1);
	TestEqual(TEXT("A second delivery is at24"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(A, 24), 2);
	TestEqual(TEXT("B has no early departure"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(B, 15.99), 0);
	TestEqual(TEXT("B first delivery is at16"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(B, 16), 1);
	TestEqual(TEXT("B charged variation is second scheduled load at52"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(B, 52), 2);
	TestEqual(TEXT("B third delivery is at88"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(B, 88), 3);
	TestEqual(TEXT("Elapsed jump resolves one current slot without a backlog"), ALSShellSupplySchedule::GetScheduledDepartureOrdinal(B, 124), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSiegeCommonAttackTracerTest,
	"Landship.ShellExperiment.Siege.CommonAttackWaitsForLoadAndExternalControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSiegeCommonAttackTracerTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!TestNotNull(TEXT("Production castle"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSOutpost* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(-4000, 0, -50), FRotator::ZeroRotator, Spawn);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(40, 40, 1));
	Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->GetStaticMeshComponent()->SetCollisionObjectType(LSCollisionChannels::Ground);
	Floor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	const FTransform HeavyTransform(FVector(-3600, 0, 140));
	auto* Heavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), HeavyTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Heavy->EnemyKind = ELSShellEnemyKind::Heavy;
	Heavy->StatsTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	auto* Siege = Heavy->FindComponentByClass<ULSShellSiegeController>();
	if (!Siege)
	{
		Siege = NewObject<ULSShellSiegeController>(Heavy);
		Heavy->AddInstanceComponent(Siege);
		Siege->RegisterComponent();
	}
	Siege->DeploymentLocation = HeavyTransform.GetLocation();
	Siege->InitialDelaySeconds = 0;
	Heavy->FinishSpawning(HeavyTransform);
	const FTransform SecondTransform(HeavyTransform.GetLocation() + FVector(0, 350, 0));
	auto* SecondHeavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), SecondTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	SecondHeavy->EnemyKind = ELSShellEnemyKind::Heavy;
	SecondHeavy->StatsTable = Heavy->StatsTable;
	auto* SecondSiege = SecondHeavy->FindComponentByClass<ULSShellSiegeController>();
	if (!SecondSiege)
	{
		SecondSiege = NewObject<ULSShellSiegeController>(SecondHeavy);
		SecondHeavy->AddInstanceComponent(SecondSiege);
		SecondSiege->RegisterComponent();
	}
	SecondSiege->DeploymentLocation = HeavyTransform.GetLocation();
	SecondSiege->InitialDelaySeconds = 0;
	SecondHeavy->FinishSpawning(SecondTransform);
	if (!TestTrue(TEXT("Actual GameMode BeginPlay"), TestWorld.BeginPlayInTestWorld())) return false;
	if (Siege->Phase == ELSShellSiegePhase::Disabled && !TestTrue(TEXT("Siege binds actual common attack"), Siege->BeginSiege(Castle, HeavyTransform.GetLocation()))) return false;
	if (SecondSiege->Phase == ELSShellSiegePhase::Disabled && !TestTrue(TEXT("Second siege binds same authored base"), SecondSiege->BeginSiege(Castle, HeavyTransform.GetLocation()))) return false;
	if (!TestTrue(TEXT("Siege initialized"), Siege->ConfigurationError.IsEmpty())) return false;
	const FLSBasicAttackSettings OriginalAttack = Heavy->BasicAttack->GetAttackSettings();
	TestEqual(TEXT("Existing Heavy damage remains30"), OriginalAttack.Damage, 30.f);
	const double FirstDistance = ULSCombatTargetLibrary::GetAttackDistance(Heavy, Castle);
	const double SecondDistance = ULSCombatTargetLibrary::GetAttackDistance(SecondHeavy, Castle);
	const bool bFirstShotClear = Heavy->BasicAttack->HasClearAttackPathFromLocation(Heavy->GetActorLocation(), Castle);
	const bool bSecondShotClear = SecondHeavy->BasicAttack->HasClearAttackPathFromLocation(SecondHeavy->GetActorLocation(), Castle);
	AddInfo(FString::Printf(TEXT("Siege occupied-depot fixture firstDistance=%.6f secondDistance=%.6f range=%.1f firstClear=%d secondClear=%d"),
		FirstDistance, SecondDistance, OriginalAttack.Range, bFirstShotClear, bSecondShotClear));
	if (!TestTrue(TEXT("Both actual current firing poses are inside unchanged common range"), FirstDistance <= OriginalAttack.Range && SecondDistance <= OriginalAttack.Range)) return false;
	if (!TestTrue(TEXT("Both actual firing paths are clear before depot selection"), bFirstShotClear && bSecondShotClear)) return false;
	for (int32 Step = 0; Step < 400 && Siege->Phase != ELSShellSiegePhase::Loading; ++Step)
	{
		++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
	}
	if (!TestTrue(TEXT("Real grounded path reaches deployed load phase"), Siege->Phase == ELSShellSiegePhase::Loading)) return false;
	if (!TestTrue(TEXT("Occupied shared base selects a distinct actual firing pose"), SecondSiege->bHasActiveDeployment &&
		!SecondSiege->ActiveDeploymentLocation.Equals(Siege->ActiveDeploymentLocation, UE_KINDA_SMALL_NUMBER) &&
		SecondSiege->ActiveDeploymentLocation.Equals(SecondTransform.GetLocation(), 1.f))) return false;
	TestEqual(TEXT("Deployed response multiplier is2 without changing actual mass"), Siege->GetResponseResistanceMultiplier(), 2.f);
	for (int32 Step = 0; Step < 790; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestEqual(TEXT("No attack before8-second load warning"), Siege->ShotsFired, 0);
	for (int32 Step = 0; Step < 50 && Siege->ShotsFired == 0; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	if (!TestEqual(TEXT("One actual common projectile attack after load"), Siege->ShotsFired, 1)) return false;
	for (int32 Step = 0; Step < 50 && SecondSiege->ShotsFired == 0; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestEqual(TEXT("Second Heavy also fires through common attack from its independent occupied-depot fallback"), SecondSiege->ShotsFired, 1);
	Siege->SetExternalControlHeld(TEXT("Cold"), true);
	Siege->SetExternalControlHeld(TEXT("Glue"), true);
	++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
	TestTrue(TEXT("Control interrupts loading or reload"), Siege->Phase == ELSShellSiegePhase::Recovering);
	Siege->SetExternalControlHeld(TEXT("Cold"), false);
	for (int32 Step = 0; Step < 500; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestEqual(TEXT("Thaw cannot resume a still-glued siege"), Siege->ShotsFired, 1);
	TestTrue(TEXT("Remaining reason retains recovery state"), Siege->Phase == ELSShellSiegePhase::Recovering);
	TestEqual(TEXT("Controller never rewrites shared attack interval"), Heavy->BasicAttack->GetAttackSettings().Interval, OriginalAttack.Interval);
	AddInfo(FString::Printf(TEXT("Siege common-attack tracer shots=%d phase=%s damage=%.1f interval=%.1f"),
		Siege->ShotsFired, *Siege->GetPhaseText(), OriginalAttack.Damage, OriginalAttack.Interval));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
