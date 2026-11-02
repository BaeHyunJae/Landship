#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastleProjectile.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"
#include "Misc/AutomationTest.h"
#include "Obstacles/LSBarricade.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

namespace
{
struct FLSCannonIntegrationPolicyScope
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	~FLSCannonIntegrationPolicyScope()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnCannonIntegrationActor(UWorld* World, const FVector& Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

float CannonIntegrationHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	return Actor->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

void PauseCannonIntegrationUnit(ALSUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
	Unit->GetCharacterMovement()->SetComponentTickEnabled(false);
}

FLSCastleWeaponSettings CannonIntegrationSettings()
{
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 5;
	Settings.Radius = 150;
	Settings.ProjectileSpeed = 1000;
	Settings.KnockbackSpeed = 350;
	Settings.KnockbackLift = 350;
	return Settings;
}

ALSCastleProjectile* LaunchIntegrationCannon(FAutomationTestBase& Test, UWorld* World, AActor* Source,
	const FVector& Start, const FVector& Aim, const FLSCastleWeaponSettings& Settings)
{
	const FTransform Transform(Start);
	auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Test.TestTrue(TEXT("실제 대포 발사"), Shot->Launch(ELSCastleWeapon::Cannon, Settings,
		ULSHealthComponent::MakeDamageRequest(Settings.Damage, Source, Source), Aim)))
	{
		Shot->Destroy();
		return nullptr;
	}
	Shot->FinishSpawning(Transform);
	return Shot;
}

void TickCannonIntegrationWorld(UWorld* World)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, .01f);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackFactionIntegrationTest,
	"Landship.Castle.CannonKnockback.FactionAndAppliedDamageIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackFactionIntegrationTest::RunTest(const FString& Parameters)
{
	FLSCannonIntegrationPolicyScope Restore;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bFriendlyFire : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = bFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = SpawnCannonIntegrationActor<ALSBasicAttackTestActor>(World, FVector(0, 0, 100));
		Source->SetInitialFaction(Faction);
		Source->Attack->SetAutoAttackOnBeginPlay(false);
		auto* Same = SpawnCannonIntegrationActor<ALSTroopUnit>(World, FVector(400, 80, 100));
		auto* Other = SpawnCannonIntegrationActor<ALSTroopUnit>(World, FVector(400, -80, 100));
		Same->SetInitialFaction(Faction);
		Other->SetInitialFaction(Faction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		for (auto* Unit : {Same, Other}) PauseCannonIntegrationUnit(Unit);
		const float SameHealth = CannonIntegrationHealth(Same), OtherHealth = CannonIntegrationHealth(Other);
		const FLSCastleWeaponSettings Settings = CannonIntegrationSettings();
		auto* Shot = LaunchIntegrationCannon(*this, World, Source, Source->GetActorLocation(), FVector(400, 0, 100), Settings);
		if (!Shot) return false;
		Shot->Tick(1);
		const FString Case = FString::Printf(TEXT("Faction=%d FF=%d "), static_cast<int32>(Faction), bFriendlyFire);
		TestEqual(*(Case + TEXT("같은 진영 실제 피해")), CannonIntegrationHealth(Same), SameHealth - (bFriendlyFire ? Settings.Damage : 0));
		TestEqual(*(Case + TEXT("상대 진영 실제 피해")), CannonIntegrationHealth(Other), OtherHealth - Settings.Damage);
		TestEqual(*(Case + TEXT("같은 진영 명령 컴포넌트 넉백")), Same->Orders->IsKnockedBack(), bFriendlyFire);
		TestTrue(*(Case + TEXT("상대 진영 명령 컴포넌트 넉백")), Other->Orders->IsKnockedBack());
		TestTrue(*(Case + TEXT("같은 진영 보류 속도")), Same->GetCharacterMovement()->PendingLaunchVelocity.Equals(
			bFriendlyFire ? FVector(0, Settings.KnockbackSpeed, Settings.KnockbackLift) : FVector::ZeroVector));
		TestTrue(*(Case + TEXT("상대 진영 보류 속도")), Other->GetCharacterMovement()->PendingLaunchVelocity.Equals(
			FVector(0, -Settings.KnockbackSpeed, Settings.KnockbackLift)));
		for (auto* Unit : {Same, Other}) Unit->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
		Shot->Tick(1);
		TestEqual(*(Case + TEXT("반복 Tick은 피해를 중복 적용하지 않는다")), CannonIntegrationHealth(Other), OtherHealth - Settings.Damage);
		for (auto* Unit : {Same, Other})
			TestTrue(*(Case + TEXT("반복 Tick은 넉백을 중복 적용하지 않는다")), Unit->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackEligibilityIntegrationTest,
	"Landship.Castle.CannonKnockback.DamageOnlyEligibilityIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackEligibilityIntegrationTest::RunTest(const FString& Parameters)
{
	FLSCannonIntegrationPolicyScope Restore;
	Restore.Settings->bAllowFriendlyFire = true;
	Restore.Settings->bAllowSelfDamage = true;
	for (int32 Case = 0; Case < 7; ++Case)
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = SpawnCannonIntegrationActor<ALSBasicAttackTestActor>(World, FVector(0, 0, 100));
		Source->Attack->SetAutoAttackOnBeginPlay(false);
		AActor* Target;
		if (Case == 4)
		{
			auto* Outpost = SpawnCannonIntegrationActor<ALSEnemyOutpost>(World, FVector(400, 0, 100));
			Outpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
			auto* Setup = SpawnCannonIntegrationActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
			Setup->PlayerCastle = SpawnCannonIntegrationActor<ALSMobileCastle>(World, FVector(10000, 0, 200));
			Setup->CoreOutpost = Outpost;
			Target = Outpost;
		}
		else if (Case == 5)
			Target = SpawnCannonIntegrationActor<ALSBarricade>(World, FVector(400, 0, 100));
		else if (Case == 2)
			Target = SpawnCannonIntegrationActor<ALSMobileCastle>(World, FVector(400, 0, 144));
		else
		{
			auto* Troop = SpawnCannonIntegrationActor<ALSTroopUnit>(World, FVector(Case == 3 ? 551 : 400, 0, 100));
			if (Case == 6) Troop->Selection->Kind = ELSSelectionKind::Building;
			Target = Troop;
		}
		Target->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		ALSUnit* Unit = Cast<ALSUnit>(Target);
		if (Unit) PauseCannonIntegrationUnit(Unit);
		const float Before = CannonIntegrationHealth(Target);
		if (!TestTrue(TEXT("실제 대상의 체력이 준비됨"), Before > 5)) return false;
		auto* Root = CastChecked<UPrimitiveComponent>(Target->GetRootComponent());
		const FTransform ActorBefore = Target->GetActorTransform(), RootBefore = Root->GetComponentTransform();
		FLSCastleWeaponSettings Settings = CannonIntegrationSettings();
		Settings.Damage = Case == 0 ? 0 : Case == 1 ? Before : 5;
		auto* Shot = LaunchIntegrationCannon(*this, World, Source, Source->GetActorLocation(), FVector(400, 0, 100), Settings);
		if (!Shot) return false;
		Shot->Tick(1);
		const FString Label = FString::Printf(TEXT("Case=%d "), Case);
		TestEqual(*(Label + TEXT("대상별 실제 피해")), CannonIntegrationHealth(Target),
			Case == 0 || Case == 3 ? Before : Case == 1 ? 0.f : Before - Settings.Damage);
		if (ULSOrderComponent* Orders = Target->FindComponentByClass<ULSOrderComponent>())
			TestFalse(*(Label + TEXT("넉백 상태가 되지 않는다")), Orders->IsKnockedBack());
		if (Unit)
			TestTrue(*(Label + TEXT("넉백 속도 없음")), Unit->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
		TestTrue(*(Label + TEXT("피해 뒤 액터 위치와 회전 유지")), Target->GetActorTransform().Equals(ActorBefore));
		TestTrue(*(Label + TEXT("피해 뒤 루트 위치와 회전 유지")), Root->GetComponentTransform().Equals(RootBefore));
		TestTrue(*(Label + TEXT("피해 뒤 루트 물리 속도 없음")), Root->GetPhysicsLinearVelocity().IsNearlyZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackCastSnapshotIntegrationTest,
	"Landship.Castle.CannonKnockback.CastWeaponSnapshotAfterUpgradeDisableAndSourceRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackCastSnapshotIntegrationTest::RunTest(const FString& Parameters)
{
	FLSCannonIntegrationPolicyScope Restore;
	Restore.Settings->bAllowFriendlyFire = true;
	Restore.Settings->bAllowSelfDamage = true;
	for (bool bDestroySource : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Castle = SpawnCannonIntegrationActor<ALSMobileCastle>(World, FVector(0, 0, 144));
		auto* Target = SpawnCannonIntegrationActor<ALSTroopUnit>(World, FVector(600, 0, 100));
		auto* Original = LoadObject<ULSCastleWeaponData>(nullptr, TEXT("/Game/Landship/Castle/Data/DA_Cannon.DA_Cannon"));
		if (!TestNotNull(TEXT("기존 대포 Data Asset"), Original)) return false;
		auto* Data = DuplicateObject<ULSCastleWeaponData>(Original, World);
		const FLSCastleWeaponSettings Settings = CannonIntegrationSettings();
		Data->Levels = {Settings, Settings};
		if (!TestTrue(TEXT("메모리 복제 데이터 지정"), Castle->Weapons->SetWeaponDefinitions({Data}))) return false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		PauseCannonIntegrationUnit(Castle);
		PauseCannonIntegrationUnit(Target);
		const float Before = CannonIntegrationHealth(Target);
		if (!TestEqual(TEXT("무장 컴포넌트의 실제 대포 발사"), Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon,
			Target->GetActorLocation()), ELSWeaponCastResult::Ready)) return false;
		ALSCastleProjectile* Shot = nullptr;
		for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) Shot = *It;
		if (!TestNotNull(TEXT("실제 생성된 포탄"), Shot)) return false;
		for (auto& Level : Data->Levels)
		{
			Level.Damage = 50;
			Level.KnockbackSpeed = 900;
			Level.KnockbackLift = 900;
		}
		TestEqual(TEXT("비행 중 강화"), Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon), ELSWeaponChangeResult::Success);
		TestEqual(TEXT("비행 중 비활성화"), Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false), ELSWeaponChangeResult::Success);
		Castle->SetActorLocation(FVector(600, 900, 144));
		if (bDestroySource) Castle->Destroy();
		Shot->Tick(1);
		TestEqual(TEXT("발사 당시 피해량 유지"), CannonIntegrationHealth(Target), Before - Settings.Damage);
		TestTrue(TEXT("중심 표적은 저장한 발사 방향과 속도를 유지"), Target->GetCharacterMovement()->PendingLaunchVelocity.Equals(
			FVector(Settings.KnockbackSpeed, 0, Settings.KnockbackLift)));
		TestTrue(TEXT("발사자 제거 여부와 무관하게 실제 넉백"), Target->Orders->IsKnockedBack());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackBattleEndIntegrationTest,
	"Landship.Castle.CannonKnockback.RemainingCannonAfterVictoryAndLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackBattleEndIntegrationTest::RunTest(const FString& Parameters)
{
	FLSCannonIntegrationPolicyScope Restore;
	Restore.Settings->bAllowFriendlyFire = true;
	Restore.Settings->bAllowSelfDamage = true;
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
	auto* Setup = SpawnCannonIntegrationActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = SpawnCannonIntegrationActor<ALSMobileCastle>(World, FVector(10000, 0, 200));
	Setup->CoreOutpost = SpawnCannonIntegrationActor<ALSEnemyOutpost>(World, FVector(0, 0, 200));
	Setup->CoreOutpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
	auto* Floor = SpawnCannonIntegrationActor<AActor>(World, FVector::ZeroVector);
	auto* Collision = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Collision);
	Floor->AddInstanceComponent(Collision);
	Collision->SetBoxExtent(FVector(40000, 4000, 25));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(LSCollisionChannels::Ground);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetMobility(EComponentMobility::Static);
	Collision->SetWorldLocation(FVector(0, 0, -25));
	Collision->RegisterComponent();
	auto* Enemy = SpawnCannonIntegrationActor<ALSTroopUnit>(World, FVector(20000, 0, 100));
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	auto* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("실제 GameState"), State) || !TestTrue(TEXT("전투 결과 판정 활성"), State->IsBattleOutcomeEnabled())) return false;
	Enemy->SetActorLocation(FVector(20000, 0, Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
	Enemy->GetCharacterMovement()->DisableMovement();
	const FVector Start = Enemy->GetActorLocation();
	const float Before = CannonIntegrationHealth(Enemy);
	const FLSCastleWeaponSettings Settings = CannonIntegrationSettings();
	auto* Shot = LaunchIntegrationCannon(*this, World, Setup->PlayerCastle, Start - FVector(600, 0, 0), Start, Settings);
	if (!Shot) return false;
	Setup->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(CannonIntegrationHealth(Setup->CoreOutpost), nullptr, nullptr));
	TickCannonIntegrationWorld(World);
	if (!TestEqual(TEXT("착탄 전에 승리 확정"), State->GetBattleOutcome(), ELSBattleOutcome::Victory)) return false;
	TestEqual(TEXT("승리 확정 때 표적 피해 없음"), CannonIntegrationHealth(Enemy), Before);
	TestEqual(TEXT("승리 확정 때 살아 있는 병력 이동 정지"), Enemy->GetCharacterMovement()->MovementMode, MOVE_None);
	Shot->Tick(1);
	if (!TestEqual(TEXT("종료 뒤 잔여 포탄 피해"), CannonIntegrationHealth(Enemy), Before - Settings.Damage)) return false;
	TestTrue(TEXT("종료 뒤 잔여 포탄 넉백"), Enemy->Orders->IsKnockedBack());
	TestEqual(TEXT("정지한 병력을 낙하 모드로 전환"), Enemy->GetCharacterMovement()->MovementMode, MOVE_Falling);
	TestTrue(TEXT("종료 뒤에도 넉백 속도 보류"), !Enemy->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
	Enemy->Restart();
	float HighestZ = Start.Z;
	for (int32 Index = 0; Index < 300 && Enemy->GetCharacterMovement()->MovementMode != MOVE_None; ++Index)
	{
		TickCannonIntegrationWorld(World);
		HighestZ = FMath::Max(HighestZ, static_cast<float>(Enemy->GetActorLocation().Z));
	}
	const FVector Landed = Enemy->GetActorLocation();
	TestTrue(TEXT("종료 뒤 실제 수평 넉백"), Landed.X > Start.X + 50);
	TestTrue(TEXT("종료 뒤 실제 상승 궤적"), HighestZ > Start.Z + 10);
	TestTrue(TEXT("실제 지면 착지"), FMath::Abs(Landed.Z - Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) < 5);
	TestEqual(TEXT("착지 뒤 이동 정지"), Enemy->GetCharacterMovement()->MovementMode, MOVE_None);
	TestFalse(TEXT("착지 뒤 공격 재시작 거부"), Enemy->BasicAttack->StartAutoAttack());
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestFalse(TEXT("착지 뒤 새 명령 거부"), Enemy->Orders->SubmitOrder(Hold));
	TestEqual(TEXT("착지 뒤 예약 명령 없음"), Enemy->Orders->GetQueuedCount(), 0);
	for (int32 Index = 0; Index < 20; ++Index) TickCannonIntegrationWorld(World);
	TestTrue(TEXT("착지 뒤 위치 유지"), Enemy->GetActorLocation().Equals(Landed, .1));
	TestEqual(TEXT("잔여 포탄 뒤에도 승리 유지"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
