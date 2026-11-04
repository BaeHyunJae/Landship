#if WITH_DEV_AUTOMATION_TESTS
#include "Controls/LSPlayerController.h"
#include "Controls/LSCommandCardComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSHealthComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
template <typename T>
T* SpawnWeaponInputActor(UWorld* World, const FVector& Location = FVector::ZeroVector)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldWeaponSelectionTest,
	"Landship.Controls.BattlefieldWeapons.SelectionIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSBattlefieldWeaponSelectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* PC = SpawnWeaponInputActor<ALSPlayerController>(World);
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144));
	auto* Unit = SpawnWeaponInputActor<ALSTroopUnit>(World, FVector(-900, 0, 96));
	auto* Setup = SpawnWeaponInputActor<ALSBattlefieldSetup>(World);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = SpawnWeaponInputActor<ALSEnemyOutpost>(World, FVector(10000, 0, 100));
	Setup->CoreOutpost->Vision->bPublicCorePosition = true;
	if (!Fixture.BeginPlayInTestWorld()) return false;
	TestTrue(TEXT("핵심 거점 수치 초기화"), Setup->CoreOutpost->IsStatsInitialized());
	TestTrue(TEXT("선택과 무관한 명시적 성"), PC->GetConfiguredBattlefieldCastle() == Castle);
	PC->ApplySelection({Unit});
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("병력 사수 명령 준비"), Unit->Orders->SubmitOrder(Hold));
	const int32 Queued = Unit->Orders->GetQueuedCount();
	TestNull(TEXT("선택 전용 성 조회 의미 유지"), PC->GetSelectedArmedCastle());
	TestTrue(TEXT("병력만 선택하고 대포 조준"), PC->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	TestTrue(TEXT("조준 미리보기의 성"), PC->GetAimingCastle() == Castle);
	TestTrue(TEXT("조준 중 병력 선택 유지"), PC->GetSelectedActors() == TArray<AActor*>({Unit}));
	PC->ApplySelection({});
	TestTrue(TEXT("선택 해제도 지정된 성의 조준 유지"), PC->GetAimingCastle() == Castle);
	TestTrue(TEXT("선택 없이 실제 시전"), PC->ConfirmCastleWeapon(FVector(300, 0, 0)) == ELSWeaponCastResult::Ready);
	TestTrue(TEXT("시전 뒤 병력 명령 유지"), Unit->Orders->GetOrderKind() == ELSOrderKind::Hold);
	TestEqual(TEXT("시전 뒤 예약 개수 유지"), Unit->Orders->GetQueuedCount(), Queued);
	TestTrue(TEXT("선택 없이 다른 무장 조준"), PC->ArmCastleWeapon(ELSCastleWeapon::Shotgun));
	PC->CancelInput();
	TestEqual(TEXT("취소는 쿨타임을 소비하지 않음"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun), 0.0f);
	PC->ApplySelection({Unit, Castle});
	TestTrue(TEXT("혼합 선택 조준"), PC->ArmCastleWeapon(ELSCastleWeapon::Ballista));
	PC->CommandCard->CycleGroup();
	TestTrue(TEXT("병종 전환은 조준 취소"), PC->GetPendingInput() == ELSCommandInput::None);
	TestEqual(TEXT("병종 전환은 선택 유지"), PC->GetSelectedActors().Num(), 2);
	TestTrue(TEXT("재조준"), PC->ArmCastleWeapon(ELSCastleWeapon::Shotgun));
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Shotgun, false);
	TestTrue(TEXT("무장 비활성화 즉시 조준 취소"), PC->GetPendingInput() == ELSCommandInput::None);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldWeaponResolutionTest,
	"Landship.Controls.BattlefieldWeapons.InvalidConfigurationAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSBattlefieldWeaponResolutionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* PC = SpawnWeaponInputActor<ALSPlayerController>(World);
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144));
	auto* Other = LSCastleTestFixture::Spawn(World, FVector(-1000, 0, 144));
	auto* Setup = SpawnWeaponInputActor<ALSBattlefieldSetup>(World);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = SpawnWeaponInputActor<ALSEnemyOutpost>(World, FVector(10000, 0, 100));
	Setup->CoreOutpost->Vision->bPublicCorePosition = true;
	if (!Fixture.BeginPlayInTestWorld()) return false;
	TestTrue(TEXT("핵심 거점 수치 초기화"), Setup->CoreOutpost->IsStatsInitialized());
	PC->ApplySelection({Other});
	TestTrue(TEXT("다른 성 선택은 지정된 성을 대체하지 않음"), PC->GetCastleWeaponSource() == Castle);
	auto* Duplicate = SpawnWeaponInputActor<ALSBattlefieldSetup>(World);
	TestNull(TEXT("중복 설정은 임의로 선택하지 않음"), PC->GetCastleWeaponSource());
	Duplicate->Destroy();
	AActor* Original = Setup->PlayerCastle;
	Setup->PlayerCastle = Other;
	TestNull(TEXT("BeginPlay 뒤 바꾼 참조는 과거 상태와 혼합하지 않음"), PC->GetCastleWeaponSource());
	Setup->PlayerCastle = Original;
	TestTrue(TEXT("정상 참조 복구"), PC->GetCastleWeaponSource() == Castle);
	Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(10000, Setup->CoreOutpost, Setup->CoreOutpost));
	TestTrue(TEXT("사망한 성은 체력 표시를 위해 유지"), PC->GetConfiguredBattlefieldCastle() == Castle);
	TestNull(TEXT("사망 시 선택한 다른 성으로 대체하지 않음"), PC->GetCastleWeaponSource());
	TestFalse(TEXT("사망 후 조준 불가"), PC->ArmCastleWeapon(ELSCastleWeapon::Ballista));
	Castle->Destroy();
	TestNull(TEXT("성 제거 뒤 오래된 참조 차단"), PC->GetConfiguredBattlefieldCastle());
	Setup->Destroy();
	TestNull(TEXT("설정 제거 뒤에도 선택 기반으로 복귀하지 않음"), PC->GetCastleWeaponSource());
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif
