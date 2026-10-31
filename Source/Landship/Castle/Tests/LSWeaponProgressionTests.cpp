#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/Tests/LSWeaponStateObserver.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include <limits>

namespace
{
ULSCastleWeaponData* CopyData(UWorld* World, const TCHAR* Name)
{
	const FString Path = FString::Printf(TEXT("/Game/Landship/Castle/Data/%s.%s"), Name, Name);
	ULSCastleWeaponData* Original = LoadObject<ULSCastleWeaponData>(nullptr, *Path);
	return Original ? DuplicateObject<ULSCastleWeaponData>(Original, World) : nullptr;
}

void AdvanceProgression(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}

float ProgressionHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	return Actor->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSWeaponStateTest, "Landship.Castle.Progression.StateAndCooldown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSWeaponStateTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	ALSWeaponStateObserver* Observer = World->SpawnActor<ALSWeaponStateObserver>();
	ULSCastleWeaponData* Cannon = CopyData(World, TEXT("DA_Cannon"));
	ULSCastleWeaponData* Shotgun = CopyData(World, TEXT("DA_Shotgun"));
	ULSCastleWeaponData* Ballista = CopyData(World, TEXT("DA_Ballista"));
	if (!TestNotNull(TEXT("대포 성장 데이터"), Cannon) || !Shotgun || !Ballista)
		return false;
	Cannon->Levels[1].Cooldown = 2;
	Cannon->Levels[1].Damage = 88;
	TestTrue(TEXT("생성 단계 데이터 지정"), Castle->Weapons->SetWeaponDefinitions({Cannon, Shotgun, Ballista}));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	Observer->Source = Castle->Weapons;
	Castle->Weapons->OnWeaponStateChanged.AddDynamic(Observer, &ALSWeaponStateObserver::Observe);
	FLSCastleWeaponState State;
	TestTrue(TEXT("초기 상태 조회"), Castle->Weapons->GetWeaponState(ELSCastleWeapon::Cannon, State));
	TestTrue(TEXT("기존 플레이를 위한 기본 1레벨 해금·활성"), State.Level == 1 && State.bUnlocked && State.bEnabled);
	Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, FVector(100, 0, 0));
	const float Remaining = Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon);
	TestTrue(TEXT("쿨타임 중 업그레이드"),
	         Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon) == ELSWeaponChangeResult::Success);
	TestEqual(TEXT("현재 레벨 수치 조회"), Castle->Weapons->FindSettings(ELSCastleWeapon::Cannon)->Damage, 88.0f);
	TestEqual(TEXT("레벨의 쿨타임 수치 변경은 현재 쿨타임을 변경하지 않음"),
	          Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), Remaining);
	TestEqual(TEXT("상태 변경 알림 한 번"), Observer->Notifications, 1);
	TestTrue(TEXT("알림 중 상태 변경 재진입 차단"), Observer->ReentrantResult == ELSWeaponChangeResult::Busy);
	ELSCastleWeapon Last;
	FVector Aim;
	FLSCastleWeaponSettings Snapshot;
	Castle->Weapons->GetRecentCast(Last, Aim, &Snapshot);
	TestEqual(TEXT("시전 표시도 이전 레벨의 스냅샷 유지"), Snapshot.Damage, 60.0f);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, false);
	TestTrue(TEXT("비활성 상태 사용 차단"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Disabled);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, true);
	TestTrue(TEXT("재활성화해도 쿨타임 유지"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Cooldown);
	const int32 Count = Observer->Notifications;
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, true);
	TestEqual(TEXT("동일 상태 요청은 알림 중복 없음"), Observer->Notifications, Count);
	Castle->Weapons->SetWeaponUnlocked(ELSCastleWeapon::Cannon, false);
	TestTrue(TEXT("잠긴 무장 사용 차단"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Locked);
	TestTrue(TEXT("잠긴 무장 강화 차단"),
	         Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon) == ELSWeaponChangeResult::Locked);
	TestTrue(TEXT("잠긴 무장 활성화 차단"),
	         Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Cannon, true) == ELSWeaponChangeResult::Locked);
	Castle->Weapons->UnlockWeapon(ELSCastleWeapon::Cannon);
	Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon);
	TestTrue(TEXT("최대 레벨에서 추가 강화 거부"),
	         Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon) == ELSWeaponChangeResult::MaxLevel);
	Castle->Weapons->GetWeaponState(ELSCastleWeapon::Cannon, State);
	TestEqual(TEXT("최대 레벨 유지"), State.Level, 3);
	TestEqual(TEXT("상태 변경으로 쿨타임 초기화 불가"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon),
	          Remaining);
	Controller->ApplySelection({Castle});
	Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Shotgun, false);
	TestTrue(TEXT("비활성 알림으로 조준 즉시 취소"), Controller->GetPendingInput() == ELSCommandInput::None);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Shotgun, true);
	Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun);
	Castle->Weapons->SetWeaponUnlocked(ELSCastleWeapon::Shotgun, false);
	TestTrue(TEXT("해금 취소 알림으로 조준 즉시 취소"), Controller->GetPendingInput() == ELSCommandInput::None);
	TestTrue(TEXT("잘못된 ID 변경 거부"),
	         Castle->Weapons->UpgradeWeapon(static_cast<ELSCastleWeapon>(255)) == ELSWeaponChangeResult::InvalidWeapon);
	TestFalse(TEXT("플레이 중 정의 교체 차단"), Castle->Weapons->SetWeaponDefinitions({Cannon}));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSWeaponProjectileLevelTest, "Landship.Castle.Progression.ProjectileSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSWeaponProjectileLevelTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSMobileCastle* Enemy = World->SpawnActor<ALSMobileCastle>(FVector(600, 0, 144), FRotator::ZeroRotator);
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	ULSCastleWeaponData* Data = CopyData(World, TEXT("DA_Ballista"));
	if (!Data)
		return false;
	Data->Levels[1].Cooldown = 5;
	Castle->Weapons->SetWeaponDefinitions({Data});
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	Castle->GetCharacterMovement()->SetComponentTickEnabled(false);
	Enemy->GetCharacterMovement()->SetComponentTickEnabled(false);
	Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(1800, 0, 0));
	Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Ballista);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Ballista, false);
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
		TestTrue(TEXT("이미 발사한 투사체 속도 보존"), FMath::IsNearlyEqual(It->GetVelocity().Size(), 1800.0));
	AdvanceProgression(World, 0.4f);
	TestEqual(TEXT("강화·비활성 이후에도 기존 투사체는 1레벨 피해 적용"), ProgressionHealth(Enemy), 840.0f);
	Castle->Weapons->SetWeaponEnabled(ELSCastleWeapon::Ballista, true);
	AdvanceProgression(World, 9.7f);
	TestTrue(TEXT("기존 10초 쿨타임 종료 후 재시전"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(1800, 0, 0)) == ELSWeaponCastResult::Ready);
	TestTrue(TEXT("새 시전에는 강화된 쿨타임 적용"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista), 5));
	AdvanceProgression(World, 0.4f);
	TestEqual(TEXT("새 투사체는 2레벨 피해 적용"), ProgressionHealth(Enemy), 640.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSWeaponInvalidDataTest, "Landship.Castle.Progression.InvalidData",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSWeaponInvalidDataTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>();
	ULSCastleWeaponData* Data = CopyData(World, TEXT("DA_Cannon"));
	if (!Data)
		return false;
	TestTrue(TEXT("부분 무장 구성 가능"), Castle->Weapons->SetWeaponDefinitions({Data}));
	TestFalse(TEXT("중복 ID 정의 거부"), Castle->Weapons->SetWeaponDefinitions({Data, Data}));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	TestTrue(TEXT("중복 정의 거부 시 기존 구성 유지"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Ready);
	TestTrue(TEXT("없는 무장을 기본 수치로 대체하지 않음"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Shotgun) == ELSWeaponCastResult::InvalidData);
	const FLSCastleWeaponSettings Saved = Data->Levels[1];
	Data->Levels[1].Radius = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("잘못된 다음 레벨 거부"),
	         Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon) == ELSWeaponChangeResult::InvalidData);
	FLSCastleWeaponState State;
	Castle->Weapons->GetWeaponState(ELSCastleWeapon::Cannon, State);
	TestEqual(TEXT("강화 실패 시 현재 레벨 유지"), State.Level, 1);
	Data->Levels[1] = Saved;
	Data->Levels.Reset();
	TestTrue(TEXT("현재 레벨 데이터 누락 차단"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, FVector(100, 0, 0)) ==
	             ELSWeaponCastResult::InvalidData);
	TestEqual(TEXT("누락 데이터는 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon),
	          0.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSWeaponUpgradedAreaTest, "Landship.Castle.Progression.UpgradedArea",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSWeaponUpgradedAreaTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>();
	ALSBasicAttackTestActor* Inside =
	    World->SpawnActor<ALSBasicAttackTestActor>(FVector(1575, 0, 100), FRotator::ZeroRotator);
	ALSBasicAttackTestActor* Outside =
	    World->SpawnActor<ALSBasicAttackTestActor>(FVector(1625, 0, 100), FRotator::ZeroRotator);
	Inside->SetInitialFaction(ELSFaction::Enemy);
	Outside->SetInitialFaction(ELSFaction::Enemy);
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	TestTrue(TEXT("1레벨 사거리 밖"), Castle->Weapons->ValidateAim(ELSCastleWeapon::Cannon, FVector(1300, 0, 0)) ==
	                                      ELSWeaponCastResult::OutOfRange);
	Castle->Weapons->UpgradeWeapon(ELSCastleWeapon::Cannon);
	TestEqual(TEXT("조준과 실행이 참조할 2레벨 반경"), Castle->Weapons->FindSettings(ELSCastleWeapon::Cannon)->Radius,
	          300.0f);
	TestTrue(TEXT("2레벨 사거리 적용"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, FVector(1300, 0, 0)) == ELSWeaponCastResult::Ready);
	TestEqual(TEXT("강화된 범위와 피해 적용"), ProgressionHealth(Inside), 20.0f);
	TestEqual(TEXT("강화 범위 밖 대상 제외"), ProgressionHealth(Outside), 100.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
