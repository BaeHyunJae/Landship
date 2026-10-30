#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "Combat/Tests/LSHealthTestObserver.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Troops/LSUnit.h"
#include "Castle/LSCastle.h"
#include "Enemies/LSOutpost.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace
{
const TCHAR* UnitPath = TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C");
const TCHAR* CastlePath = TEXT("/Game/Developers/BHJ/CombatStats/BP_TestCastle.BP_TestCastle_C");
const TCHAR* OutpostPath = TEXT("/Game/Developers/BHJ/CombatStats/BP_TestOutpost.BP_TestOutpost_C");

template <typename T>
T* SpawnTarget(UWorld* World, const TCHAR* Path)
{
	UClass* Class = LoadClass<T>(nullptr, Path);
	if (!Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Class, FVector(0, 0, 200), FRotator::ZeroRotator, Parameters);
}

ULSHealthComponent* GetHealth(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<ULSHealthComponent>() : nullptr;
}

FLSDamageRequest MakeDamage(float Amount, AActor* Source)
{
	FLSDamageRequest Request;
	Request.DamageAmount = Amount;
	Request.SourceActor = Source;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatActorHealthIntegrationTest, "Landship.Combat.Health.ActorIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatActorHealthIntegrationTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bOriginalFriendlyFire; };
	Settings->bAllowFriendlyFire = true;
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create game world"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSUnit* Ally = SpawnTarget<ALSUnit>(World, UnitPath);
	ALSUnit* Enemy = SpawnTarget<ALSUnit>(World, UnitPath);
	ALSCastle* Castle = SpawnTarget<ALSCastle>(World, CastlePath);
	ALSOutpost* Outpost = SpawnTarget<ALSOutpost>(World, OutpostPath);
	if (!TestNotNull(TEXT("Ally unit"), Ally) || !TestNotNull(TEXT("Enemy unit"), Enemy)
		|| !TestNotNull(TEXT("Castle"), Castle) || !TestNotNull(TEXT("Outpost"), Outpost))
	{
		return false;
	}
	for (AActor* Actor : TArray<AActor*>{Ally, Enemy, Castle, Outpost})
	{
		if (!TestNotNull(TEXT("Native health subobject"), GetHealth(Actor))) { return false; }
	}
	Ally->SetInitialFaction(ELSFaction::Ally);
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	GetHealth(Ally)->bDestroyOnDeath = false;
	bool bHealthReadyInParent = false;
	FDelegateHandle ParentProbe = APawn::OnPawnBeginPlay.AddLambda([&](APawn* Pawn)
	{
		if (Pawn == Ally)
		{
			FLSHealthSnapshot Snapshot;
			bHealthReadyInParent = GetHealth(Ally)->TryGetHealth(Snapshot) && Snapshot.CurrentHealth > 0;
		}
	});
	const bool bBeganPlay = TestWorld.BeginPlayInTestWorld();
	APawn::OnPawnBeginPlay.Remove(ParentProbe);
	if (!TestTrue(TEXT("Begin play"), bBeganPlay))
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	TestTrue(TEXT("Health ready inside parent pawn BeginPlay"), bHealthReadyInParent);
	for (AActor* Actor : TArray<AActor*>{Ally, Enemy, Castle, Outpost})
	{
		FLSHealthSnapshot Snapshot;
		TestTrue(TEXT("Real asset health ready"), GetHealth(Actor)->TryGetHealth(Snapshot));
		TestTrue(TEXT("Initial health is positive"), Snapshot.CurrentHealth > 0);
	}
	TestTrue(TEXT("Ally automatically targets enemy"), ULSCombatTargetLibrary::CanAutoAttackTarget(Ally, Enemy));
	TestTrue(TEXT("Enemy automatically targets ally"), ULSCombatTargetLibrary::CanAutoAttackTarget(Enemy, Ally));
	TestFalse(TEXT("Ally does not automatically target castle"), ULSCombatTargetLibrary::CanAutoAttackTarget(Ally, Castle));
	TestFalse(TEXT("Enemy does not automatically target outpost"), ULSCombatTargetLibrary::CanAutoAttackTarget(Enemy, Outpost));
	TestEqual(TEXT("Ally friendly fire applies"), GetHealth(Castle)->ApplyDamage(MakeDamage(1, Ally)).Status, ELSApplyDamageStatus::Applied);
	TestEqual(TEXT("Enemy friendly fire applies"), GetHealth(Outpost)->ApplyDamage(MakeDamage(1, Enemy)).Status, ELSApplyDamageStatus::Applied);
	{
		const FString Prefix = TEXT("[LSUnit::SetInitialFaction] Faction is locked:");
		AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 1);
		FEnsureScope Ensure([this, &Prefix](const FEnsureHandlerArgs& Args)
		{
			if (!FString(Args.Message).StartsWith(Prefix)) { return false; }
			AddError(Args.Message);
			return true;
		});
		Ally->SetInitialFaction(ELSFaction::Enemy);
		TestEqual(TEXT("Mutation emits an actual ensure"), Ensure.GetCount(), 1);
		TestEqual(TEXT("Initialized faction unchanged"), Ally->GetFaction(), ELSFaction::Ally);
	}
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(World);
	GetHealth(Ally)->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	Observer->DeathCallback = [&](const FLSDamageResult& Damage)
	{
		TestEqual(TEXT("Movement disabled before public death"), static_cast<EMovementMode>(Ally->GetCharacterMovement()->MovementMode), MOVE_None);
		TestEqual(TEXT("Capsule disabled before public death"), Ally->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		FLSHealthSnapshot Snapshot;
		TestTrue(TEXT("Dead snapshot available during event"), GetHealth(Ally)->TryGetHealth(Snapshot) && Snapshot.bIsDead);
		TestEqual(TEXT("Death source preserved"), Damage.SourceActor.Get(), static_cast<AActor*>(Enemy));
		TestEqual(TEXT("Death payload health zero"), Damage.CurrentHealth, 0.0f);
	};
	FLSHealthSnapshot BeforeDeath;
	GetHealth(Ally)->TryGetHealth(BeforeDeath);
	const FLSApplyDamageResult Death = GetHealth(Ally)->ApplyDamage(MakeDamage(BeforeDeath.CurrentHealth + 100, Enemy));
	TestEqual(TEXT("Overkill returns actual health lost"), Death.AppliedDamage, BeforeDeath.CurrentHealth);
	TestEqual(TEXT("One public death event"), Observer->DeathResults.Num(), 1);
	TestFalse(TEXT("Preserved dead actor is not destroyed"), Ally->IsActorBeingDestroyed());
	TestFalse(TEXT("Dead unit cannot be selected"), ULSCombatTargetLibrary::CanAutoAttackTarget(Enemy, Ally));

	ALSUnit* Deferred = World->SpawnActorDeferred<ALSUnit>(Ally->GetClass(), FTransform(FVector(300, 0, 200)),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (TestNotNull(TEXT("Deferred unit"), Deferred))
	{
		Deferred->SetInitialFaction(ELSFaction::Enemy);
		Deferred->FinishSpawning(FTransform(FVector(300, 0, 200)));
		FLSHealthSnapshot Snapshot;
		TestTrue(TEXT("Deferred spawn initializes health"), GetHealth(Deferred)->TryGetHealth(Snapshot));
		TestEqual(TEXT("Deferred initial faction retained"), Deferred->GetFaction(), ELSFaction::Enemy);
	}
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatHealthValidationIntegrationTest, "Landship.Combat.Health.ValidationIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatHealthValidationIntegrationTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : {UnitPath, CastlePath, OutpostPath})
	{
		UClass* Class = LoadClass<AActor>(nullptr, Path);
		if (!TestNotNull(TEXT("Baseline class"), Class)) { return false; }
		FDataValidationContext Context;
		TestEqual(TEXT("Baseline CDO validates"), Class->GetDefaultObject()->IsDataValid(Context), EDataValidationResult::Valid);
		TestEqual(TEXT("Baseline validation has no errors"), Context.GetNumErrors(), 0u);
	}
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create invalid configuration world"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSUnit* BadStats = SpawnTarget<ALSUnit>(World, UnitPath);
	ALSUnit* BadRagdoll = SpawnTarget<ALSUnit>(World, UnitPath);
	ALSUnit* Duplicate = SpawnTarget<ALSUnit>(World, UnitPath);
	ALSUnit* BadDelay = SpawnTarget<ALSUnit>(World, UnitPath);
	if (!BadStats || !BadRagdoll || !Duplicate || !BadDelay) { AddError(TEXT("Missing validation fixtures")); return false; }
	for (ALSUnit* Actor : {BadStats, BadRagdoll, Duplicate, BadDelay})
	{
		if (!TestNotNull(TEXT("Validation health subobject"), GetHealth(Actor))) { return false; }
	}
	FStructProperty* StatsProperty = FindFProperty<FStructProperty>(ALSUnit::StaticClass(), TEXT("StatsRow"));
	FBoolProperty* RagdollProperty = FindFProperty<FBoolProperty>(ALSUnit::StaticClass(), TEXT("bUseRagdoll"));
	if (!StatsProperty || !RagdollProperty) { AddError(TEXT("Missing configurable properties")); return false; }
	*StatsProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(BadStats) = FDataTableRowHandle();
	RagdollProperty->SetPropertyValue_InContainer(BadRagdoll, true);
	BadRagdoll->GetMesh()->SetSkeletalMeshAsset(nullptr);
	ULSHealthComponent* Extra = NewObject<ULSHealthComponent>(Duplicate);
	Duplicate->AddInstanceComponent(Extra);
	Extra->RegisterComponent();
	GetHealth(BadDelay)->DestroyDelay = -1;
	TArray<ALSUnit*> InvalidActors{BadStats, BadRagdoll, Duplicate, BadDelay};
	{
		FEnsureScope UnexpectedEnsure([this](const FEnsureHandlerArgs& Args)
		{
			AddError(FString::Printf(TEXT("Editor validation unexpectedly ensured: %s"), Args.Message));
			return true;
		});
		for (ALSUnit* Actor : InvalidActors)
		{
			FDataValidationContext Context;
			TestEqual(TEXT("Invalid instance rejected before play"), Actor->IsDataValid(Context), EDataValidationResult::Invalid);
			TestTrue(TEXT("Invalid instance explains configuration"), Context.GetNumErrors() > 0);
		}
		TestEqual(TEXT("Editor validation is non-diagnostic"), UnexpectedEnsure.GetCount(), 0);
	}
	const FString StatsPrefix = TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed:");
	const FString HealthPrefix = TEXT("[LSHealthComponent::InitializeHealth] Owner=");
	AddExpectedErrorPlain(StatsPrefix, EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(HealthPrefix, EAutomationExpectedErrorFlags::Contains, 3);
	FEnsureScope ExpectedEnsure([this, &StatsPrefix, &HealthPrefix](const FEnsureHandlerArgs& Args)
	{
		const FString Message(Args.Message);
		if (!Message.StartsWith(StatsPrefix) && !Message.StartsWith(HealthPrefix)) { return false; }
		AddError(Message);
		return true;
	});
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(World);
	for (ALSUnit* Actor : InvalidActors)
	{
		GetHealth(Actor)->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	}
	TestTrue(TEXT("Invalid instances still reach BeginPlay"), TestWorld.BeginPlayInTestWorld());
	TestEqual(TEXT("Each invalid actor diagnosed once"), ExpectedEnsure.GetCount(), 4);
	for (ALSUnit* Actor : InvalidActors)
	{
		TestEqual(TEXT("Failed health state persists"), GetHealth(Actor)->GetInitializationState(), ELSHealthInitializationState::Failed);
		FLSHealthSnapshot Sentinel;
		Sentinel.CurrentHealth = 456;
		TestFalse(TEXT("Failed health query blocked"), GetHealth(Actor)->TryGetHealth(Sentinel));
		TestEqual(TEXT("Failed query preserves output"), Sentinel.CurrentHealth, 456.0f);
		TestEqual(TEXT("Failure disables movement"), static_cast<EMovementMode>(Actor->GetCharacterMovement()->MovementMode), MOVE_None);
		TestEqual(TEXT("Failure disables capsule"), Actor->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Failure does not activate ragdoll"), Actor->GetMesh()->IsSimulatingPhysics());
		TestFalse(TEXT("Failure does not destroy actor"), Actor->IsActorBeingDestroyed());
	}
	TestEqual(TEXT("Initialization failures are not deaths"), Observer->DeathResults.Num(), 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatHumanoidDeathIntegrationTest, "Landship.Combat.Health.HumanoidDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatHumanoidDeathIntegrationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Create humanoid game world"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	ALSUnit* Unit = SpawnTarget<ALSUnit>(TestWorld.GetTestWorld(),
		TEXT("/Game/Developers/BHJ/CombatStats/BP_TestHumanoid.BP_TestHumanoid_C"));
	if (!TestNotNull(TEXT("Configured humanoid Blueprint"), Unit)) { return false; }
	if (!TestNotNull(TEXT("Humanoid health subobject"), GetHealth(Unit))) { return false; }
	GetHealth(Unit)->bDestroyOnDeath = false;
	TestNotNull(TEXT("Humanoid skeletal mesh"), Unit->GetMesh()->GetSkeletalMeshAsset());
	TestNotNull(TEXT("Humanoid physics asset"), Unit->GetMesh()->GetPhysicsAsset());
	if (!TestTrue(TEXT("Begin humanoid play"), TestWorld.BeginPlayInTestWorld())) { return false; }
	FLSHealthSnapshot Snapshot;
	if (!TestTrue(TEXT("Humanoid health ready"), GetHealth(Unit)->TryGetHealth(Snapshot))) { return false; }
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(TestWorld.GetTestWorld());
	GetHealth(Unit)->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	Observer->DeathCallback = [&](const FLSDamageResult&)
	{
		FLSHealthSnapshot Dead;
		TestTrue(TEXT("Death committed before public callback"), GetHealth(Unit)->TryGetHealth(Dead) && Dead.bIsDead);
		TestTrue(TEXT("Ragdoll enabled before public callback"), Unit->GetMesh()->IsSimulatingPhysics());
		TestEqual(TEXT("Corpse blocks ground"), Unit->GetMesh()->GetCollisionResponseToChannel(LSCollisionChannels::Ground), ECR_Block);
		TestEqual(TEXT("Corpse ignores units"), Unit->GetMesh()->GetCollisionResponseToChannel(LSCollisionChannels::Unit), ECR_Ignore);
		TestEqual(TEXT("Corpse ignores projectiles"), Unit->GetMesh()->GetCollisionResponseToChannel(LSCollisionChannels::Projectile), ECR_Ignore);
		TestEqual(TEXT("Capsule does not obstruct"), Unit->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	};
	TestEqual(TEXT("Lethal humanoid hit applied"), GetHealth(Unit)->ApplyDamage(MakeDamage(Snapshot.CurrentHealth, nullptr)).Status, ELSApplyDamageStatus::Applied);
	TestEqual(TEXT("One humanoid death"), Observer->DeathResults.Num(), 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
