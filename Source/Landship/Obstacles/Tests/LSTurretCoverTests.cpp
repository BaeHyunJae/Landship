#if WITH_DEV_AUTOMATION_TESTS

#include "Obstacles/LSTurret.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/BoxComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

struct FLSTurretCoverTestAccess
{
	static AActor* Select(ALSTurret* Turret) { return Turret->ResolveTarget(); }
	static void Refresh(ALSTurret* Turret) { Turret->UpdateThreatVisual(); }
	static UMaterialInstanceDynamic* Material(ALSTurret* Turret) { return Turret->ThreatMaterial; }
};

namespace
{
UBoxComponent* SpawnTurretCover(UWorld* World, FVector Position, FVector Extent, bool bEnabled)
{
	AActor* Wall = World->SpawnActor<AActor>();
	auto* Body = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Body);
	Wall->AddInstanceComponent(Body);
	Body->SetBoxExtent(Extent);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Body->RegisterComponent();
	Wall->SetActorLocation(Position);
	return Body;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTurretCoverSelectionTest,
	"Landship.Obstacles.Turret.ClearShotSelectionAndThreat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTurretCoverSelectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Turret = World->SpawnActor<ALSTurret>(FVector(0, 0, 120), FRotator::ZeroRotator, Spawn);
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(800, 0, 120));
	auto* Troop = World->SpawnActor<ALSTroopUnit>(FVector(0, 800, 88), FRotator::ZeroRotator, Spawn);
	if (!Turret || !Castle || !Troop) return false;
	Castle->Orders->SetComponentTickEnabled(false);
	Troop->Orders->SetComponentTickEnabled(false);
	Troop->BasicAttack->SetAutoAttackOnBeginPlay(false);
	Troop->GetCharacterMovement()->DisableMovement();
	UBoxComponent* CastleWall = SpawnTurretCover(World, FVector(400, 0, 200), FVector(40, 180, 200), true);
	UBoxComponent* TroopWall = SpawnTurretCover(World, FVector(0, 400, 200), FVector(180, 40, 200), false);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	auto* Attack = Turret->FindComponentByClass<ULSBasicAttackComponent>();
	auto* Material = FLSTurretCoverTestAccess::Material(Turret);
	if (!TestNotNull(TEXT("actual turret attack component"), Attack) || !TestNotNull(TEXT("actual turret threat MID"), Material)) return false;
	TestTrue(TEXT("castle is within unchanged turret range"), ULSCombatTargetLibrary::GetAttackDistance(Turret, Castle) <= Attack->GetAttackSettings().Range);
	TestFalse(TEXT("actual terrain wall hides castle from turret"), Attack->HasClearAttackPathFromLocation(Turret->GetActorLocation(), Castle));
	TestTrue(TEXT("troop remains exposed"), Attack->HasClearAttackPathFromLocation(Turret->GetActorLocation(), Troop));
	TestEqual(TEXT("hidden castle does not suppress exposed troop selection"), FLSTurretCoverTestAccess::Select(Turret), static_cast<AActor*>(Troop));
	TestTrue(TEXT("actual turret attack accepts exposed troop"), Attack->StartAttack(Troop));
	TestTrue(TEXT("actual shot enters cooldown"), Attack->GetCooldownRemaining() > 0);
	FLSTurretCoverTestAccess::Refresh(Turret);
	TestTrue(TEXT("clear actual target stays orange during cooldown"), Material->K2_GetVectorParameterValue(TEXT("TeamColor")).Equals(FLinearColor(1, .35f, .03f)));

	TroopWall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("second actual wall hides troop"), Attack->HasClearAttackPathFromLocation(Turret->GetActorLocation(), Troop));
	TestNull(TEXT("all hidden targets leave resolver empty"), FLSTurretCoverTestAccess::Select(Turret));
	FLSTurretCoverTestAccess::Refresh(Turret);
	TestTrue(TEXT("blocked cached target immediately removes orange during cooldown"), Material->K2_GetVectorParameterValue(TEXT("TeamColor")).Equals(FLinearColor(.95f, .12f, .05f)));

	CastleWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TroopWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestEqual(TEXT("clearing cover restores castle priority among exposed targets"), FLSTurretCoverTestAccess::Select(Turret), static_cast<AActor*>(Castle));
	TestTrue(TEXT("clear castle can become actual attack target during cooldown"), Attack->StartAttack(Castle));
	FLSTurretCoverTestAccess::Refresh(Turret);
	TestTrue(TEXT("restored clear castle target restores orange"), Material->K2_GetVectorParameterValue(TEXT("TeamColor")).Equals(FLinearColor(1, .35f, .03f)));
	TestEqual(TEXT("stationary turret never repositions around cover"), Turret->GetActorLocation(), FVector(0, 0, 120));
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif
