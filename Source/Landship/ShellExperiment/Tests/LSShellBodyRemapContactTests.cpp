#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FRemapContactDamagePolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FRemapContactDamagePolicy() { Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true; }
	~FRemapContactDamagePolicy()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnRemapContactActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

void TickRemapContactWorld(UWorld* World)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, .01f);
}

ALSShellExperimentCoordinator* SetUpRemapContactWorld(UWorld* World, FAutomationTestBase& Test)
{
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!Test.TestNotNull(TEXT("Production castle"), Castle)) return nullptr;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!Test.TestNotNull(TEXT("Production experiment core class"), CoreClass)) return nullptr;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!Test.TestNotNull(TEXT("Production core"), Core)) return nullptr;
	auto* Setup = SpawnRemapContactActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = SpawnRemapContactActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyStage = ELSShellSupplyStage::Complete;
	Coordinator->AvailableModules = {ELSShellModule::Gravity, ELSShellModule::Blast, ELSShellModule::Split,
		ELSShellModule::Electric, ELSShellModule::Thrust, ELSShellModule::Iron, ELSShellModule::Adhesion, ELSShellModule::Cooling};
	Coordinator->InitialFirst = ELSShellModule::Cooling;
	Coordinator->InitialSecond = ELSShellModule::Blast;
	return Coordinator;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellQueuedContactRemapTest,
	"Landship.ShellExperiment.Material.QueuedContactFollowsOnlyContinuingExplicitBodyRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellQueuedContactRemapTest::RunTest(const FString& Parameters)
{
	FRemapContactDamagePolicy Policy;
	for (bool bSeparated : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpRemapContactWorld(World, *this);
		if (!Coordinator) return false;
		auto* Core = Coordinator->CoreOutpost.Get();
		auto* CoreBody = CastChecked<UPrimitiveComponent>(Core->GetRootComponent());
		const FBox CoreBox = CoreBody->Bounds.GetBox();
		const FVector EnemyLocation(CoreBox.Min.X - 42.5, CoreBody->Bounds.Origin.Y, CoreBody->Bounds.Origin.Z);
		const FTransform EnemyPose(EnemyLocation);
		auto* Enemy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), EnemyPose,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("Production Assault"), Enemy)) return false;
		Enemy->EnemyKind = ELSShellEnemyKind::Assault;
		Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
			TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
		Enemy->FinishSpawning(EnemyPose);
		if (!TestWorld.BeginPlayInTestWorld() || !TestTrue(TEXT("Complete-stage coordinator active"), Coordinator->IsBattleActive())) return false;
		if (!TestNull(TEXT("Core is an actual damage endpoint without a Reactive BodyId"),
			Core->FindComponentByClass<ULSShellReactiveComponent>())) return false;

		FLSShellShot Cooling;
		Coordinator->CaptureShotContext(Cooling);
		Cooling.ColdSeconds = 100;
		FHitResult CoolingHit(Enemy, Enemy->GetCapsuleComponent(), EnemyLocation, -FVector::ForwardVector);
		CoolingHit.ImpactPoint = EnemyLocation - FVector(42, 0, 0);
		Coordinator->ExecuteModule(ELSShellModule::Cooling, CoolingHit.ImpactPoint, Cooling, CoolingHit, true);
		if (!TestTrue(TEXT("Production Cooling makes the actual Assault capsule physical"), Enemy->GetCapsuleComponent()->IsSimulatingPhysics())) return false;
		Enemy->GetCapsuleComponent()->SetEnableGravity(false);
		if (!TestTrue(TEXT("Technical contact pair has ready collision within the actual3cmregion"),
			CoreBody->IsQueryCollisionEnabled() && Enemy->GetCapsuleComponent()->IsQueryCollisionEnabled() &&
			CoreBody->OverlapComponent(Enemy->GetCapsuleComponent()->GetComponentLocation(),
				Enemy->GetCapsuleComponent()->GetComponentQuat(), Enemy->GetCapsuleComponent()->GetCollisionShape(3)))) return false;
		const uint64 BodyId = Enemy->Reactive->BodyId;
		const uint32 Generation = Enemy->Reactive->GetBodyGeneration();
		FLSShellShot Environment;
		Coordinator->CaptureShotContext(Environment, Enemy);
		if (!TestTrue(TEXT("Original physical source is Enemy environmental provenance"),
			Environment.SourceId != 0 && Environment.Faction == ELSFaction::Enemy && !Environment.bPlayerResource)) return false;
		TestEqual(TEXT("Live Assault stores exactly180environmental charge"), Coordinator->ApplyCharge(Enemy, Environment, 180), 180.f);

		// 실제 Hit 생성은 별도 native-contact 시험의 범위다. 이 기술용 표본은 사망 예약보다 뒤의 델리게이트 순서를 고정한다.
		FLSShellShot Lethal;
		Coordinator->CaptureShotContext(Lethal);
		Lethal.First = ELSShellModule::Blast;
		Lethal.bChild = true;
		Lethal.bInheritedModule = false;
		Lethal.Damage = 500;
		Lethal.SupplyTuning.DirectMomentum = 0;
		Coordinator->QueueShellHit(Lethal, CoolingHit);
		const FVector ContactPoint(CoreBox.Min.X, EnemyLocation.Y, EnemyLocation.Z);
		FHitResult ContactHit(Core, CoreBody, ContactPoint, -FVector::ForwardVector);
		ContactHit.ImpactPoint = ContactPoint;
		if (!TestTrue(TEXT("Controlled100cm/s input reaches the production pre-contact momentum sampler"),
			Enemy->Reactive->ApplyMomentum(FVector::ForwardVector * Enemy->Reactive->GetPhysicsMassKg() * 100))) return false;
		const float SampleSpeed = FMath::Abs(FVector::DotProduct(
			Enemy->Reactive->GetSampledSurfaceVelocity(ContactPoint) - Core->GetVelocity(), ContactHit.ImpactNormal));
		if (!TestTrue(TEXT("Actual captured contact speed is charge-eligible and below mechanical damage"),
			SampleSpeed >= Coordinator->SupplyTuning.DischargeMinimumSpeed && SampleSpeed < Coordinator->Tuning.ImpactMinimumSpeed)) return false;
		Enemy->GetCapsuleComponent()->OnComponentHit.Broadcast(Enemy->GetCapsuleComponent(), Core, CoreBody, FVector::ZeroVector, ContactHit);
		const uint64 Epoch = Enemy->Reactive->GetContinuingContactEpoch(CoreBody);
		if (!TestTrue(TEXT("Production Hit delegate assigns a continuing-contact identity"), Epoch != 0)) return false;
		Coordinator->QueueContact(Enemy, Core, ContactPoint, -FVector::ForwardVector,
			FVector(100, 0, 0), FVector::ZeroVector, Epoch);
		if (bSeparated)
		{
			if (!TestTrue(TEXT("Technical separation actually moves the physical capsule"), Enemy->SetActorLocation(
				EnemyLocation + FVector(0, 1000, 0), false, nullptr, ETeleportType::TeleportPhysics))) return false;
			if (!TestFalse(TEXT("Separated pair is outside the actual3cmcontact region before death"),
				CoreBody->OverlapComponent(Enemy->GetCapsuleComponent()->GetComponentLocation(),
					Enemy->GetCapsuleComponent()->GetComponentQuat(), Enemy->GetCapsuleComponent()->GetCollisionShape(3)))) return false;
		}
		TWeakObjectPtr<ALSShellExperimentEnemy> Previous = Enemy;
		TickRemapContactWorld(World);
		TestFalse(TEXT("Earlier queued ShellHit commits common death and destroys the original Assault"), Previous.IsValid());
		TArray<ALSShellInertIce*> Ices;
		for (TActorIterator<ALSShellInertIce> It(World); It; ++It) Ices.Add(*It);
		if (!TestEqual(TEXT("One actual ice body results from common physical death"), Ices.Num(), 1)) return false;
		auto* Ice = Ices[0];
		Ice->GetPhysicalBody()->SetEnableGravity(false);
		TestEqual(TEXT("Explicit transaction preserves body identity"), Ice->Reactive->BodyId, BodyId);
		TestEqual(TEXT("Explicit transaction advances body generation"), Ice->Reactive->GetBodyGeneration(), Generation + 1);
		FLSShellShot RemappedSource;
		Coordinator->CaptureShotContext(RemappedSource, Ice);
		TestEqual(TEXT("New inert actor retains immutable original source identity"), RemappedSource.SourceId, Environment.SourceId);
		TestTrue(TEXT("New inert actor retains Enemy faction after the old source is gone"), RemappedSource.Faction == Environment.Faction);
		TestEqual(TEXT("Only a continuing contact survives the transaction"), Ice->Reactive->GetContinuingContactEpoch(CoreBody), bSeparated ? uint64(0) : Epoch);
		TestEqual(TEXT("Eligible queued contact discharges once; separated contact never discharges"), Coordinator->DischargeCount, bSeparated ? 0 : 1);
		TestEqual(TEXT("One original contact budget is consumed without copying charge"), Ice->Reactive->GetStoredCharge(), bSeparated ? 180.f : 90.f);
		FLSHealthSnapshot CoreHealth;
		if (!TestTrue(TEXT("Production core health remains readable"), Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(CoreHealth))) return false;
		TestEqual(TEXT("Original environmental charge reaches common core health without player armor"), CoreHealth.CurrentHealth, bSeparated ? 1000.f : 910.f);
		if (!bSeparated)
		{
			FLSDamageResult Damage;
			if (!TestTrue(TEXT("Common health records the transferred contact's actual discharge"), Core->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(Damage))) return false;
			TestTrue(TEXT("Discharge retains pre-contact point and destroyed original source faction"),
				Damage.bHasAttackOrigin && Damage.AttackOrigin.Equals(ContactPoint, .01) &&
				Damage.bHasSourceFaction && Damage.SourceFaction == ELSFaction::Enemy && !Damage.Source.IsValid());
			Coordinator->QueueContact(Ice, Core, ContactPoint, -FVector::ForwardVector, FVector(100, 0, 0), FVector::ZeroVector, Epoch);
			TickRemapContactWorld(World);
			TestEqual(TEXT("A mirrored or repeated callback for the transferred epoch never repeats the discharge"), Coordinator->DischargeCount, 1);
			TestEqual(TEXT("Repeated epoch preserves the exact remaining charge"), Ice->Reactive->GetStoredCharge(), 90.f);
		}
		TestEqual(TEXT("Controlled low-speed contact adds no mechanical damage event"), Coordinator->ContactCount, 0);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

#endif
