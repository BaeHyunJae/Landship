#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"
#include <limits>

namespace
{
void AdvanceWeapons(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}

ALSBasicAttackTestActor* Target(UWorld* World, const FVector& Location, ELSFaction Faction = ELSFaction::Enemy)
{
	ALSBasicAttackTestActor* Actor = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Actor->SetInitialFaction(Faction);
	return Actor;
}

float Health(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Component = Actor->FindComponentByClass<ULSHealthComponent>();
	return Component && Component->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

void StopUnitOrders(ALSUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
}

ALSBasicProjectile* Projectile(UWorld* World)
{
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
		return *It;
	return nullptr;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonAimTest, "Landship.Castle.Weapons.CannonAndAim",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCannonAimTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSMobileCastle* EnemyCastle = World->SpawnActor<ALSMobileCastle>(FVector(800, 200, 144), FRotator::ZeroRotator);
	EnemyCastle->SetInitialFaction(ELSFaction::Enemy);
	ALSBasicAttackTestActor* Enemy = Target(World, FVector(800, 0, 144));
	ALSBasicAttackTestActor* Ally = Target(World, FVector(850, -100, 144), ELSFaction::Ally);
	ALSBasicAttackTestActor* Outside = Target(World, FVector(1100, 0, 144));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	TestFalse(TEXT("미선택 시 조준 불가"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->ApplySelection({EnemyCastle});
	TestFalse(TEXT("적 성 조준 불가"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->ApplySelection({Castle});
	TestTrue(TEXT("Q 조준 준비"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->CancelInput();
	TestEqual(TEXT("취소 시 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), 0.0f);
	Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon);
	TestTrue(TEXT("사거리 밖 거부"),
	         Controller->ConfirmCastleWeapon(FVector(2000, 0, 0)) == ELSWeaponCastResult::OutOfRange);
	TestTrue(TEXT("잘못된 클릭 뒤 조준 유지"), Controller->GetPendingInput() == ELSCommandInput::CastleWeapon);
	TestEqual(TEXT("사거리 밖 실패는 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon),
	          0.0f);
	TestTrue(TEXT("유효 지면 시전"), Controller->ConfirmCastleWeapon(FVector(800, 0, 0)) == ELSWeaponCastResult::Ready);
	TestTrue(TEXT("시전 뒤 조준 해제"), Controller->GetPendingInput() == ELSCommandInput::None);
	TestEqual(TEXT("원 안의 적 피해"), Health(Enemy), 40.0f);
	TestEqual(TEXT("원 안의 적 성 피해"), Health(EnemyCastle), 940.0f);
	TestEqual(TEXT("원 밖 적 제외"), Health(Outside), 100.0f);
	TestEqual(TEXT("아군 피해 없음"), Health(Ally), 100.0f);
	TestTrue(TEXT("대포 쿨타임 8초"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), 8));
	TestFalse(TEXT("쿨타임 중 재조준 차단"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	TestTrue(TEXT("다른 무장 조준은 독립"), Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun));
	Controller->ApplySelection({EnemyCastle});
	TestTrue(TEXT("성 선택 상실 후 시전 불가"),
	         Controller->ConfirmCastleWeapon(FVector(800, 0, 0)) == ELSWeaponCastResult::Unavailable);
	TestEqual(TEXT("선택 상실은 산탄 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun),
	          0.0f);
	TestTrue(TEXT("NaN 조준 거부"),
	         Castle->Weapons->ValidateAim(ELSCastleWeapon::Ballista, FVector(std::numeric_limits<double>::quiet_NaN(),
	                                                                         0, 0)) == ELSWeaponCastResult::InvalidAim);
	Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
	    ULSHealthComponent::MakeDamageRequest(10000, EnemyCastle, EnemyCastle));
	TestTrue(TEXT("파괴된 성의 무장 차단"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Shotgun) == ELSWeaponCastResult::Unavailable);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunTest, "Landship.Castle.Weapons.ConeAndKnockback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShotgunTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSMobileCastle* Structure = World->SpawnActor<ALSMobileCastle>(FVector(500, 200, 144), FRotator::ZeroRotator);
	Structure->SetInitialFaction(ELSFaction::Enemy);
	TArray<ALSTroopUnit*> Units;
	const FVector Locations[] = {FVector(400, 0, 96), FVector(400, 400, 96), FVector(700, 0, 96),
	                             FVector(400, -200, 96)};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ALSTroopUnit* Unit = World->SpawnActor<ALSTroopUnit>(Locations[Index], FRotator::ZeroRotator);
		Unit->SetInitialFaction(Index == 3 ? ELSFaction::Ally : ELSFaction::Enemy);
		Units.Add(Unit);
	}
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	StopUnitOrders(Castle);
	StopUnitOrders(Structure);
	for (ALSTroopUnit* Unit : Units)
		StopUnitOrders(Unit);
	TestTrue(TEXT("영벡터 방향 거부"),
	         Castle->Weapons->ValidateAim(ELSCastleWeapon::Shotgun, Castle->GetActorLocation()) ==
	             ELSWeaponCastResult::InvalidAim);
	TestTrue(TEXT("전방 부채꼴 시전"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Shotgun, FVector(1000, 0, 0)) == ELSWeaponCastResult::Ready);
	TestEqual(TEXT("부채꼴 안 병력 피해"), Health(Units[0]), 85.0f);
	TestEqual(TEXT("각도 밖 병력 제외"), Health(Units[1]), 120.0f);
	TestEqual(TEXT("거리 밖 병력 제외"), Health(Units[2]), 120.0f);
	TestEqual(TEXT("아군 피해 제외"), Health(Units[3]), 120.0f);
	TestEqual(TEXT("구조물은 피해만 적용"), Health(Structure), 965.0f);
	TestTrue(TEXT("산탄 쿨타임 6초"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun), 6));
	AdvanceWeapons(World, 0.25f);
	TestTrue(TEXT("적 일반 병력이 실제로 밀려남"), Units[0]->GetActorLocation().X > 460);
	TestTrue(TEXT("적 성은 밀려나지 않음"), FVector::Dist2D(Structure->GetActorLocation(), FVector(500, 200, 144)) < 1);
	TestTrue(TEXT("아군은 밀려나지 않음"), FVector::Dist2D(Units[3]->GetActorLocation(), Locations[3]) < 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBallistaTest, "Landship.Castle.Weapons.BallistaFirstEnemy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBallistaTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSBasicAttackTestActor* Ally = Target(World, FVector(200, 0, 144), ELSFaction::Ally);
	ALSBasicAttackTestActor* Enemy = Target(World, FVector(500, 0, 144));
	ALSBasicAttackTestActor* Behind = Target(World, FVector(800, 0, 144));
	Behind->FindComponentByClass<UPrimitiveComponent>()->SetCollisionObjectType(LSCollisionChannels::Building);
	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Wall->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(20, 100, 200));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(300, 0, 144));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	TestTrue(TEXT("발리스타 직선 발사"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(1800, 0, 0)) == ELSWeaponCastResult::Ready);
	TestEqual(TEXT("발사 즉시 피해 없음"), Health(Enemy), 100.0f);
	TestTrue(TEXT("발리스타 쿨타임 10초"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista), 10));
	Castle->Destroy();
	AdvanceWeapons(World, 0.1f);
	TestEqual(TEXT("비행 중 피해 없음"), Health(Enemy), 100.0f);
	AdvanceWeapons(World, 0.3f);
	TestEqual(TEXT("지형·아군을 통과해 첫 적에게 큰 피해"), Health(Enemy), 0.0f);
	TestEqual(TEXT("뒤의 적 성·거점은 관통하지 않음"), Health(Behind), 100.0f);
	TestEqual(TEXT("아군 피해 없음"), Health(Ally), 100.0f);
	TestNull(TEXT("첫 적 명중 뒤 투사체 제거"), Projectile(World));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBallistaRangeTest, "Landship.Castle.Weapons.BallistaRange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBallistaRangeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSBasicAttackTestActor* Far = Target(World, FVector(2000, 0, 144));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(3000, 0, 0));
	Castle->GetCharacterMovement()->SetComponentTickEnabled(false);
	AdvanceWeapons(World, 1.2f);
	TestEqual(TEXT("사거리 밖 적은 피해 없음"), Health(Far), 100.0f);
	TestNull(TEXT("최대 비행 거리 후 제거"), Projectile(World));
	TestTrue(TEXT("다른 무장은 쿨타임 독립"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Ready);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
