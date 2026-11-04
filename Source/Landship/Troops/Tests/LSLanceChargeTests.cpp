#if WITH_DEV_AUTOMATION_TESTS
#include "Troops/LSCavalryComponent.h"
#include "Troops/LSSpearmanComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSLanceChargeTest, "Landship.Troops.LanceCharge.GuardAndLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSLanceChargeTest::RunTest(const FString& Parameters)
{
	FLSSpearmanBraceSettings BraceSettings;
	TestTrue(TEXT("기본 방어 설정 유효"), BraceSettings.IsValid());
	BraceSettings.GuardMultiplier = 1;
	TestFalse(TEXT("무효한 방어 배율 거부"), BraceSettings.IsValid());
	FLSCavalryChargeSettings Settings;
	TestTrue(TEXT("기본 돌격 설정 유효"), Settings.IsValid());
	Settings = FLSCavalryChargeSettings();
	Settings.RunUpDistance = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("유한하지 않은 도움닫기 거부"), Settings.IsValid());
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = Play.GetTestWorld();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Spear =
	    World->SpawnActor<ALSTroopUnit>(ALSTroopUnit::StaticClass(), FVector(0, 0, 200), FRotator(0, 180, 0), Spawn);
	auto* Cavalry = World->SpawnActor<ALSTroopUnit>(ALSTroopUnit::StaticClass(), FVector(-1000, 0, 200),
	                                                FRotator::ZeroRotator, Spawn);
	if (!Spear || !Cavalry)
	{
		return false;
	}
	Cavalry->SetInitialTroopType(ELSTroopType::Cavalry);
	Cavalry->SetInitialFaction(ELSFaction::Enemy);
	if (!Play.BeginPlayInTestWorld())
	{
		return false;
	}
	for (ALSTroopUnit* Unit : {Spear, Cavalry})
	{
		Unit->Orders->SetComponentTickEnabled(false);
		Unit->BasicAttack->StopAttack();
		Unit->GetCharacterMovement()->DisableMovement();
	}
	TestFalse(TEXT("일반 RTS에서는 돌격 기본 비활성"),
	          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->TryStartCharge(Spear));
	for (ALSTroopUnit* Unit : {Spear, Cavalry})
	{
		Unit->CombatBehavior->SetBattleEnabled(true);
	}
	const float Speed = Cavalry->GetCharacterMovement()->MaxWalkSpeed;
	TestTrue(TEXT("충분히 떨어진 적에게 돌격 시작"),
	         Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->TryStartCharge(Spear));
	TestTrue(TEXT("돌격 주행 속도 증가"), Cavalry->GetCharacterMovement()->MaxWalkSpeed > Speed);
	Cavalry->SetActorLocation(FVector(-650, 0, 200));
	// 도움닫기 거리는 위치 변화로 검증한다. 순간 이동은 돌격으로 인정하지 않는다.
	static_cast<UActorComponent*>(Cavalry->CombatBehavior.Get())->TickComponent(0.016f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("순간 이동으로 도움닫기 우회 금지"),
	          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->IsCharging());
	TestEqual(TEXT("취소하면 원래 속도 복원"), Cavalry->GetCharacterMovement()->MaxWalkSpeed, Speed);
	Cavalry->SetActorLocation(FVector(-1000, 0, 200));
	TestTrue(TEXT("명중 전 취소는 돌격 소비하지 않음"),
	         Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->TryStartCharge(Spear));
	FLSHealthSnapshot InitialHealth;
	Spear->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(InitialHealth);
	const float HealthBefore = InitialHealth.CurrentHealth;
	for (int32 Index = 0; Index < 60 && Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->IsCharging(); ++Index)
	{
		Cavalry->SetActorLocation(Cavalry->GetActorLocation() + FVector(16, 0, 0));
		Cavalry->GetCharacterMovement()->Velocity = FVector(800, 0, 0);
		static_cast<UActorComponent*>(Cavalry->CombatBehavior.Get())->TickComponent(0.02f, LEVELTICK_All, nullptr);
		if (Index > 15)
		{
			Cast<ULSSpearmanComponent>(Spear->CombatBehavior)->TryBrace(Cavalry);
		}
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.02f);
	}
	TestEqual(TEXT("최초 적 한 명에게 한 번 명중"),
	          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->GetImpactCount(), 1);
	TestTrue(TEXT("준비된 정면 창병 방어 소비"), Cast<ULSSpearmanComponent>(Spear->CombatBehavior)->HasSpentGuard());
	TestTrue(TEXT("방어된 돌격 판정"), Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->WasLastImpactGuarded());
	FLSHealthSnapshot Snapshot;
	Spear->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot);
	TestTrue(TEXT("돌격을 완전히 무효화하지 않고 피해 감소"),
	         Snapshot.CurrentHealth < HealthBefore &&
	             Snapshot.CurrentHealth >
	                 HealthBefore - Cavalry->BasicAttack->GetAttackSettings().Damage *
	                                    Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->Settings.DamageMultiplier);
	Cavalry->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot);
	TestTrue(TEXT("공통 체력 경로로 반격 피해"), Snapshot.CurrentHealth < HealthBefore);
	TestFalse(TEXT("명중한 돌격 재사용 거부"),
	          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->TryStartCharge(Spear));
	Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->SetBattleEnabled(false);
	TestFalse(TEXT("전투 종료 시 틱 정리"),
	          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->IsComponentTickEnabled());
	TestEqual(TEXT("전투 종료 시 이동 속도 복원"), Cavalry->GetCharacterMovement()->MaxWalkSpeed, Speed);
	TestTrue(TEXT("공격 경직 지연 설정"), Spear->BasicAttack->DelayNextAttack(2));
	Spear->BasicAttack->StopAttack();
	Spear->BasicAttack->StartAttack(Cavalry);
	TestTrue(TEXT("공격 재시작으로 경직 우회 금지"), Spear->BasicAttack->GetCooldownRemaining() > 1.9f);
	TestFalse(TEXT("잘못된 지연 거부"), Spear->BasicAttack->DelayNextAttack(-1));
	Play.ForwardErrorMessages(this);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSLanceExposureTest, "Landship.Troops.LanceCharge.InterceptionAndExposure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSLanceExposureTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game) || !Play.BeginPlayInTestWorld())
	{
		return false;
	}
	UWorld* World = Play.GetTestWorld();
	auto SpawnUnit = [World](ELSTroopType Type, ELSFaction Faction, const FVector& Position)
	{
		const FTransform Transform(FRotator::ZeroRotator, Position);
		auto* Unit = World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr,
		                                                     ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			return Unit;
		}
		Unit->SetInitialTroopType(Type);
		Unit->SetInitialFaction(Faction);
		Unit->FinishSpawning(Transform);
		Unit->Orders->SetComponentTickEnabled(false);
		Unit->BasicAttack->StopAttack();
		Unit->GetCharacterMovement()->DisableMovement();
		Unit->CombatBehavior->SetBattleEnabled(true);
		return Unit;
	};
	auto AdvanceCharge = [World](ALSTroopUnit* Cavalry, ALSTroopUnit* Spear, bool bBrace, bool bRear)
	{
		for (int32 Index = 0; Index < 70 && Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->IsCharging(); ++Index)
		{
			Cavalry->SetActorLocation(Cavalry->GetActorLocation() + FVector(16, 0, 0));
			Cavalry->GetCharacterMovement()->Velocity = FVector(800, 0, 0);
			static_cast<UActorComponent*>(Cavalry->CombatBehavior.Get())->TickComponent(0.02f, LEVELTICK_All, nullptr);
			if (bBrace && Cast<ULSSpearmanComponent>(Spear->CombatBehavior)->TryBrace(Cavalry) && bRear)
			{
				Spear->SetActorRotation(FRotator::ZeroRotator);
			}
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.02f);
		}
	};
	for (int32 Case = 0; Case < 3; ++Case)
	{
		const FVector Origin(0, Case * 2000, 200);
		auto* Spear = SpawnUnit(ELSTroopType::Spearman, ELSFaction::Ally, Origin);
		auto* Archer = SpawnUnit(ELSTroopType::Archer, ELSFaction::Ally, Origin + FVector(300, 0, 0));
		auto* Cavalry = SpawnUnit(ELSTroopType::Cavalry, ELSFaction::Enemy, Origin - FVector(1000, 0, 0));
		if (!Spear || !Archer || !Cavalry)
		{
			return false;
		}
		FLSHealthSnapshot Before, After;
		Archer->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Before);
		TestTrue(TEXT("후열 궁병 대상 돌격 시작"),
		         Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->TryStartCharge(Archer));
		AdvanceCharge(Cavalry, Spear, Case != 2, Case == 1);
		TestEqual(TEXT("전열에 첫 돌격 명중"), Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->GetImpactCount(), 1);
		Archer->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(After);
		TestEqual(TEXT("창병을 통과하여 후열을 때리지 않음"), After.CurrentHealth, Before.CurrentHealth);
		TestEqual(TEXT("후방/미준비는 방어 없음"),
		          Cast<ULSCavalryComponent>(Cavalry->CombatBehavior)->WasLastImpactGuarded(), Case == 0);
		if (Case == 0)
		{
			Spear->GetCharacterMovement()->StopMovementImmediately();
			auto* Second = SpawnUnit(ELSTroopType::Cavalry, ELSFaction::Enemy, Origin - FVector(1000, 0, 0));
			if (!Second)
			{
				return false;
			}
			Cavalry->Destroy();
			TestTrue(TEXT("두 번째 기마병 돌격 시작"),
			         Cast<ULSCavalryComponent>(Second->CombatBehavior)->TryStartCharge(Archer));
			AdvanceCharge(Second, Spear, true, false);
			TestEqual(TEXT("두 번째 돌격도 명중"), Cast<ULSCavalryComponent>(Second->CombatBehavior)->GetImpactCount(),
			          1);
			TestFalse(TEXT("소비한 창병 방어는 다음 기마병에 재발동하지 않음"),
			          Cast<ULSCavalryComponent>(Second->CombatBehavior)->WasLastImpactGuarded());
		}
	}
	Play.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopBehaviorOwnershipTest, "Landship.Troops.LanceCharge.BehaviorOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopBehaviorOwnershipTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = Play.GetTestWorld();
	TArray<ALSTroopUnit*> Units;
	for (ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Archer, ELSTroopType::Cavalry})
	{
		ALSTroopUnit* Unit = World->SpawnActor<ALSTroopUnit>();
		if (!Unit)
		{
			return false;
		}
		Unit->SetInitialTroopType(Type);
		Units.Add(Unit);
	}
	if (!Play.BeginPlayInTestWorld())
	{
		return false;
	}
	for (ALSTroopUnit* Unit : Units)
	{
		TArray<ULSTroopCombatComponent*> Behaviors;
		Unit->GetComponents(Behaviors);
		TestEqual(TEXT("병종별 전투 컴포넌트는 정확히 하나"), Behaviors.Num(), 1);
		if (!TestNotNull(TEXT("공통 조회가 실제 컴포넌트를 가리킴"), Unit->CombatBehavior.Get()))
		{
			return false;
		}
		TestTrue(TEXT("기본 병종 설정 유효"), Unit->CombatBehavior->IsConfigurationValid());
		TestFalse(TEXT("준비 단계에는 틱 없음"), Unit->CombatBehavior->IsComponentTickEnabled());
		TestEqual(TEXT("기병에만 기병 컴포넌트"), !!Cast<ULSCavalryComponent>(Unit->CombatBehavior),
		          Unit->GetTroopType() == ELSTroopType::Cavalry);
		TestEqual(TEXT("창병에만 창병 컴포넌트"), !!Cast<ULSSpearmanComponent>(Unit->CombatBehavior),
		          Unit->GetTroopType() == ELSTroopType::Spearman);
		Unit->CombatBehavior->SetBattleEnabled(true);
		TestFalse(TEXT("전투 시작만으로 매 프레임 틱하지 않음"), Unit->CombatBehavior->IsComponentTickEnabled());
		Unit->CombatBehavior->ApplyStagger(0.2f, FVector::ZeroVector);
		TestTrue(TEXT("공통 경직은 모든 병종에 적용"), Unit->CombatBehavior->IsStaggered());
		TestTrue(TEXT("경직 중에만 틱 활성"), Unit->CombatBehavior->IsComponentTickEnabled());
		Unit->CombatBehavior->SetBattleEnabled(false);
		TestFalse(TEXT("비활성화 시 경직 정리"), Unit->CombatBehavior->IsStaggered());
		TestFalse(TEXT("비활성화 시 틱 정리"), Unit->CombatBehavior->IsComponentTickEnabled());
	}
	// 잘못된 병종에 특화 컴포넌트를 붙여도 활성화되지 않는다.
	ULSCavalryComponent* WrongRole = NewObject<ULSCavalryComponent>(Units[0]);
	TestFalse(TEXT("창병 소유 기병 컴포넌트 설정 거부"), WrongRole->IsConfigurationValid());
	WrongRole->SetBattleEnabled(true);
	TestFalse(TEXT("잘못된 병종 활성화 거부"), WrongRole->IsBattleEnabled());
	return true;
}

#endif
