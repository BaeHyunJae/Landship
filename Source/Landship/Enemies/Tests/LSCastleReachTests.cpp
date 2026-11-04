#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/Tests/LSEnemySupportTestAccess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMeleeCastleReachTest, "Landship.Enemies.MeleeCanDamageCastleAtContact",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMeleeCastleReachTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("게임 월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UWorld* World = TestWorld.GetTestWorld();
	ALSTroopUnit* Troop = World->SpawnActor<ALSTroopUnit>(FVector(0, 0, 88), FRotator::ZeroRotator, Spawn);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(400, 0, 142), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("기존 공용 근접병"), Troop) || !TestNotNull(TEXT("기존 이동형 성"), Castle))
	{
		return false;
	}
	Troop->SetInitialFaction(ELSFaction::Enemy);
	if (!TestTrue(TEXT("실제 병종 초기화"), Troop->SetInitialTroopType(ELSTroopType::Spearman)) ||
	    !TestTrue(TEXT("전투 준비"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	// 자율 추적과 중력이 접촉 위치를 바꾸기 전에 실제 캡슐 충돌만 검사한다.
	Troop->Orders->SetComponentTickEnabled(false);
	Castle->Orders->SetComponentTickEnabled(false);
	Troop->BasicAttack->StopAttack();

	FHitResult Contact;
	Troop->GetCharacterMovement()->SafeMoveUpdatedComponent(FVector(500, 0, 0), Troop->GetActorQuat(), true,
	                                                       Contact);
	if (!TestTrue(TEXT("성 충돌 외곽에 실제 접촉"), Contact.bBlockingHit && Contact.GetActor() == Castle))
	{
		return false;
	}
	ULSHealthComponent* Health = Castle->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot Before;
	if (!TestNotNull(TEXT("성 체력"), Health) || !TestTrue(TEXT("공격 전 체력 조회"), Health->TryGetHealth(Before)))
	{
		return false;
	}
	const double OriginDistance = FVector::Distance(Troop->GetActorLocation(), Castle->GetActorLocation());
	AddInfo(FString::Printf(TEXT("Contact: Range=%.2f OriginDistance=%.2f TroopRadius=%.2f CastleRadius=%.2f"),
	                        Troop->BasicAttack->GetAttackSettings().Range, OriginDistance,
	                        Troop->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
	                        Castle->GetCapsuleComponent()->GetUnscaledCapsuleRadius()));
	TestTrue(TEXT("성 지정 공격 수락"), Troop->BasicAttack->StartAttack(Castle));
	FLSHealthSnapshot After;
	if (!TestTrue(TEXT("공격 후 체력 조회"), Health->TryGetHealth(After)))
	{
		return false;
	}
	TestTrue(TEXT("근접병은 성 충돌 외곽에 닿으면 실제 피해를 준다"), After.CurrentHealth < Before.CurrentHealth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyCastleBoundaryStrikeTest,
	"Landship.Enemies.Behavior.CastleBoundaryRangeEntryAndRepeatedMelee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyCastleBoundaryStrikeTest::RunTest(const FString& Parameters)
{
	for (const float CastleRadius : {142.0f, 500.0f})
	{
		for (const ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Cavalry})
		{
			FTestWorldWrapper TestWorld;
			if (!TestWorld.CreateTestWorld(EWorldType::Game)) { return false; }
			UWorld* World = TestWorld.GetTestWorld();
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ALSBattlefieldSetup* Setup = World->SpawnActor<ALSBattlefieldSetup>();
			ALSEnemyOutpost* Core = World->SpawnActor<ALSEnemyOutpost>(FVector(0, -500, 100), FRotator::ZeroRotator, Spawn);
			ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(1000, 0, CastleRadius), FRotator::ZeroRotator, Spawn);
			ALSEnemyTroop* Troop = World->SpawnActor<ALSEnemyTroop>(FVector(0, 0, 88), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("전장 설정"), Setup) || !TestNotNull(TEXT("수비 거점"), Core) ||
				!TestNotNull(TEXT("성"), Castle) || !TestNotNull(TEXT("적 근접병"), Troop)) { return false; }
			Setup->PlayerCastle = Castle;
			Setup->CoreOutpost = Core;
			Castle->GetCapsuleComponent()->SetCapsuleSize(CastleRadius, CastleRadius);
			Castle->GetMovementComponent()->Deactivate();
			Troop->GetCharacterMovement()->DisableMovement();
			Troop->Behavior->GuardedOutpost = Core;
			if (!Troop->SetInitialTroopType(Type) || !TestWorld.BeginPlayInTestWorld()) { return false; }
			Core->Behavior->SetComponentTickEnabled(false);
			Core->BasicAttack->Deactivate();
			Castle->Orders->SetComponentTickEnabled(false);

			const float TroopRadius = Troop->GetCapsuleComponent()->GetScaledCapsuleRadius();
			Castle->SetActorLocation(FVector(CastleRadius + TroopRadius + 20, 0, CastleRadius));
			FLSBasicAttackSettings Settings;
			Settings.Damage = 10;
			Settings.Interval = 0.1f;
			Settings.Range = 30;
			if (!TestTrue(TEXT("근접 공격 설정"), Troop->BasicAttack->ConfigureAttack(Settings))) { return false; }
			TestTrue(TEXT("중심 간 거리는 사거리보다 길다"), FVector::Dist(Troop->GetActorLocation(), Castle->GetActorLocation()) > Settings.Range);
			TestTrue(TEXT("충돌 원통 외곽은 사거리 안이다"), ULSCombatTargetLibrary::GetAttackDistance(Troop, Castle) <= Settings.Range);
			TestEqual(TEXT("타격 직전 해석기가 큰 성도 찾는다"), FLSEnemySupportTestAccess::StrikeTarget(Troop->Behavior), static_cast<AActor*>(Castle));
			FLSHealthSnapshot Before;
			ULSHealthComponent* Health = Castle->FindComponentByClass<ULSHealthComponent>();
			if (!Health || !Health->TryGetHealth(Before)) { return false; }
			// 정기 판단을 늦춘 상태에서 외곽 사거리 진입만으로 즉시 판단하는지 검사한다.
			FLSEnemySupportTestAccess::WaitForRangeEntry(Troop->Behavior, Castle);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.01f);
			FLSHealthSnapshot First;
			if (!Health->TryGetHealth(First)) { return false; }
			TestTrue(TEXT("다음 정기 판단 전 외곽 사거리 진입으로 첫 타격"), First.CurrentHealth < Before.CurrentHealth);
			for (int32 Step = 0; Step < 35; ++Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, 0.01f);
			}
			FLSHealthSnapshot After;
			if (!Health->TryGetHealth(After)) { return false; }
			TestTrue(TEXT("실제 적 행동과 타격 해석기로 반복 피해"), After.CurrentHealth <= Before.CurrentHealth - Settings.Damage * 3);
			TestFalse(TEXT("적 이동은 플레이어 명령 틱을 쓰지 않는다"), Troop->Orders->IsComponentTickEnabled());
			TestWorld.ForwardErrorMessages(this);
		}
	}
	return true;
}

#endif
