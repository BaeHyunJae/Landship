#if WITH_DEV_AUTOMATION_TESTS

#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
ALSBasicAttackTestActor* SpawnVisionCombatant(UWorld* World, ELSFaction Faction, const FVector& Location)
{
	if (!ALSBattlefieldSettings::Find(World))
	{
		World->SpawnActor<ALSBattlefieldSettings>();
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSBasicAttackTestActor* Actor =
		World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator, Parameters);
	if (Actor)
	{
		Actor->SetInitialFaction(Faction);
		Actor->Attack->SetAutoAttackOnBeginPlay(false);
		Actor->HealthComponent->bDestroyOnDeath = false;
	}
	return Actor;
}

ULSVisionParticipantComponent* AddAttackVision(AActor* Actor, float Radius = 0)
{
	ULSVisionParticipantComponent* Vision = NewObject<ULSVisionParticipantComponent>(Actor);
	Vision->bProvidesSight = Radius > 0;
	Vision->SetSightRadius(Radius);
	Actor->AddInstanceComponent(Vision);
	Vision->RegisterComponent();
	return Vision;
}

bool ConfigureVisionAttack(ALSBasicAttackTestActor* Source)
{
	FLSBasicAttackSettings Settings;
	Settings.Damage = 10;
	Settings.Range = 1000;
	Settings.Interval = 1;
	return Source->Attack->ConfigureAttack(Settings);
}

void AdvanceVisionCombat(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionDirectAttackTest, "Landship.Vision.Attack.HiddenDirectTarget",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionDirectAttackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnVisionCombatant(World, ELSFaction::Ally, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Target = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(500, 0, 100));
	if (!Source || !Target)
	{
		AddError(TEXT("Spawn failed"));
		return false;
	}
	ULSVisionParticipantComponent* Sight = AddAttackVision(Source, 100);
	AddAttackVision(Target);
	if (!TestTrue(TEXT("Configure attack"), ConfigureVisionAttack(Source)) ||
		!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("Vision subsystem"), Vision))
	{
		return false;
	}
	TestFalse(TEXT("Registered target begins outside sight"), Vision->IsActorVisibleToFriendly(Target));
	TestFalse(TEXT("Direct attack rejects hidden enemy in attack range"),
			  Source->Attack->StartAttack(Target));
	TestNull(TEXT("Rejected target is not retained"), Source->Attack->GetAttackTarget());
	AdvanceVisionCombat(World, 0.1f);
	TestEqual(TEXT("Rejected attack applies no damage"), Target->GetCurrentHealth(), 100.0f);
	Sight->SetSightRadius(600);
	TestTrue(TEXT("Target becomes visible"), Vision->IsActorVisibleToFriendly(Target));
	TestTrue(TEXT("Same target accepted after reveal"), Source->Attack->StartAttack(Target));
	TestEqual(TEXT("Visible target receives real damage"), Target->GetCurrentHealth(), 90.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionAutoReacquireTest, "Landship.Vision.Attack.AutoReacquire",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionAutoReacquireTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnVisionCombatant(World, ELSFaction::Ally, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Near = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(200, 0, 100));
	ALSBasicAttackTestActor* Far = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(500, 0, 100));
	if (!Source || !Near || !Far)
	{
		AddError(TEXT("Spawn failed"));
		return false;
	}
	ULSVisionParticipantComponent* Sight = AddAttackVision(Source, 250);
	AddAttackVision(Near);
	AddAttackVision(Far);
	if (!TestTrue(TEXT("Configure attack"), ConfigureVisionAttack(Source)) ||
		!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	TestTrue(TEXT("Start auto attack"), Source->Attack->StartAutoAttack());
	TestEqual(TEXT("Visible nearest enemy acquired"), Source->Attack->GetAttackTarget(),
			  static_cast<AActor*>(Near));
	TestEqual(TEXT("First target damaged"), Near->GetCurrentHealth(), 90.0f);
	Sight->SetSightRadius(0);
	AdvanceVisionCombat(World, 1.1f);
	TestNull(TEXT("Auto search cannot reacquire either hidden enemy"), Source->Attack->GetAttackTarget());
	TestEqual(TEXT("Hidden former target receives no further hits"), Near->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("Hidden alternative receives no hits"), Far->GetCurrentHealth(), 100.0f);
	Near->SetActorLocation(FVector(800, 0, 100));
	Sight->SetSightRadius(600);
	AdvanceVisionCombat(World, 0.1f);
	TestEqual(TEXT("Auto search resumes with visible alternative"), Source->Attack->GetAttackTarget(),
			  static_cast<AActor*>(Far));
	TestEqual(TEXT("Reacquired alternative damaged"), Far->GetCurrentHealth(), 90.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionEnemyAttackTest, "Landship.Vision.Attack.EnemyIgnoresFriendlySight",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionEnemyAttackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Target = SpawnVisionCombatant(World, ELSFaction::Ally, FVector(500, 0, 100));
	if (!Source || !Target)
	{
		AddError(TEXT("Spawn failed"));
		return false;
	}
	AddAttackVision(Source);
	AddAttackVision(Target);
	if (!TestTrue(TEXT("Configure attack"), ConfigureVisionAttack(Source)) ||
		!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("Vision subsystem"), Vision))
	{
		return false;
	}
	TestEqual(TEXT("No friendly sight sources"), Vision->GetSightCircles().Num(), 0);
	TestTrue(TEXT("Enemy source hidden from friendly view"), Source->IsHidden());
	TestTrue(TEXT("Enemy direct attack remains valid"), Source->Attack->StartAttack(Target));
	TestEqual(TEXT("Enemy deals damage without friendly sight"), Target->GetCurrentHealth(), 90.0f);
	Source->Attack->StopAttack();
	AdvanceVisionCombat(World, 1.1f);
	TestTrue(TEXT("Enemy auto acquisition remains valid"), Source->Attack->StartAutoAttack());
	TestEqual(TEXT("Enemy auto attack acquires allied target"), Source->Attack->GetAttackTarget(),
			  static_cast<AActor*>(Target));
	TestEqual(TEXT("Enemy auto attack deals damage"), Target->GetCurrentHealth(), 80.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionAttackOrderQueueTest,
								 "Landship.Vision.Attack.SightLossAdvancesQueue",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionAttackOrderQueueTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnVisionCombatant(World, ELSFaction::Ally, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Target = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(500, 0, 100));
	ALSBasicAttackTestActor* QueuedTarget =
		SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(550, 0, 100));
	if (!Source || !Target || !QueuedTarget)
	{
		AddError(TEXT("Spawn failed"));
		return false;
	}
	ULSVisionParticipantComponent* Sight = AddAttackVision(Source, 600);
	AddAttackVision(Target);
	AddAttackVision(QueuedTarget);
	ULSSelectableComponent* Selection = NewObject<ULSSelectableComponent>(Source);
	Selection->SetPlayerControlled(true);
	Source->AddInstanceComponent(Selection);
	Selection->RegisterComponent();
	ULSOrderComponent* Orders = NewObject<ULSOrderComponent>(Source);
	Source->AddInstanceComponent(Orders);
	Orders->RegisterComponent();
	if (!TestTrue(TEXT("Configure attack"), ConfigureVisionAttack(Source)) ||
		!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	FLSOrder Attack;
	Attack.Kind = ELSOrderKind::AttackTarget;
	Attack.Target = Target;
	TestTrue(TEXT("Visible explicit attack accepted"), Orders->SubmitOrder(Attack));
	FLSOrder QueuedAttack = Attack;
	QueuedAttack.Target = QueuedTarget;
	TestTrue(TEXT("Queue second visible target"), Orders->SubmitOrder(QueuedAttack, true));
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("Queue Hold"), Orders->SubmitOrder(Hold, true));
	TestEqual(TEXT("Attack and Hold queued"), Orders->GetQueuedCount(), 2);
	TestEqual(TEXT("Initial explicit attack hit"), Target->GetCurrentHealth(), 90.0f);
	Sight->SetSightRadius(0);
	AdvanceVisionCombat(World, 0.15f);
	TestEqual(TEXT("Sight loss finishes attack and starts Hold"), Orders->GetOrderKind(), ELSOrderKind::Hold);
	TestEqual(TEXT("Queue consumed"), Orders->GetQueuedCount(), 0);
	TestEqual(TEXT("Hidden queued attack skipped without damage"), QueuedTarget->GetCurrentHealth(), 100.0f);
	TestNull(TEXT("Hold does not engage hidden target"), Orders->GetEngagedTarget());
	TestNull(TEXT("Attack component stops hidden engagement"), Source->Attack->GetAttackTarget());
	TestFalse(TEXT("New explicit order rejects hidden target"), Orders->SubmitOrder(Attack));
	TestEqual(TEXT("Rejected order preserves Hold"), Orders->GetOrderKind(), ELSOrderKind::Hold);
	AdvanceVisionCombat(World, 1.1f);
	TestEqual(TEXT("No further hidden damage"), Target->GetCurrentHealth(), 90.0f);
	Sight->SetSightRadius(600);
	AdvanceVisionCombat(World, 0.15f);
	TestEqual(TEXT("Hold reacquires revealed enemy"), Orders->GetEngagedTarget(),
			  static_cast<AActor*>(Target));
	TestEqual(TEXT("Hold resumes damage"), Target->GetCurrentHealth(), 80.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionProjectileSightLossTest,
								 "Landship.Vision.Attack.ProjectileSurvivesSightLoss",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionProjectileSightLossTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnVisionCombatant(World, ELSFaction::Ally, FVector(0, 0, 100));
	ALSBasicAttackTestActor* Target = SpawnVisionCombatant(World, ELSFaction::Enemy, FVector(500, 0, 100));
	if (!Source || !Target)
	{
		AddError(TEXT("Spawn failed"));
		return false;
	}
	ULSVisionParticipantComponent* Sight = AddAttackVision(Source, 600);
	AddAttackVision(Target);
	FLSProjectileAttackSettings Projectile;
	Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
	Projectile.Speed = 1000;
	Projectile.Lifetime = 2;
	if (!TestTrue(TEXT("Configure attack"), ConfigureVisionAttack(Source)) ||
		!TestTrue(TEXT("Configure projectile before BeginPlay"),
				  Source->Attack->ConfigureProjectile(Projectile)) ||
		!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("Vision subsystem"), Vision))
	{
		return false;
	}
	TestEqual(TEXT("One friendly sight circle before launch"), Vision->GetSightCircles().Num(), 1);
	TestTrue(TEXT("Launch toward visible target"), Source->Attack->StartAttack(Target));
	TWeakObjectPtr<ALSBasicProjectile> Shot;
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
	{
		Shot = *It;
		break;
	}
	if (!TestTrue(TEXT("Actual projectile spawned"), Shot.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Projectile adds no sight circle"), Vision->GetSightCircles().Num(), 1);
	Sight->SetSightRadius(0);
	TestFalse(TEXT("Target immediately leaves friendly sight"), Vision->IsActorVisibleToFriendly(Target));
	TestEqual(TEXT("Only the source circle remains during flight"), Vision->GetSightCircles().Num(), 1);
	if (Vision->GetSightCircles().Num() == 1)
	{
		TestEqual(TEXT("Source circle has zero radius"), Vision->GetSightCircles()[0].Radius, 0.0);
	}
	AdvanceVisionCombat(World, 0.25f);
	TestTrue(TEXT("Projectile advances while target hidden"),
			 Shot.IsValid() && Shot->GetActorLocation().X > 200);
	TestEqual(TEXT("No premature damage"), Target->GetCurrentHealth(), 100.0f);
	TestNull(TEXT("Sight loss stops future launches"), Source->Attack->GetAttackTarget());
	AdvanceVisionCombat(World, 0.4f);
	TestEqual(TEXT("Already launched projectile hits hidden target"), Target->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("Actual health component receives one hit"), Target->HitsReceived, 1);
	TestFalse(TEXT("Projectile removed on hit"), Shot.IsValid());
	TestEqual(TEXT("Projectile adds no circle before removal"), Vision->GetSightCircles().Num(), 1);
	TestFalse(TEXT("Hit leaves target concealed"), Vision->IsActorVisibleToFriendly(Target));
	AdvanceVisionCombat(World, 0.6f);
	TestEqual(TEXT("No second shot after sight loss"), Target->HitsReceived, 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
