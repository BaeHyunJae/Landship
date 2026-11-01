#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHazardDamagePolicyTest, "Landship.Combat.Health.HazardFactionPolicy",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHazardDamagePolicyTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	struct FRestoreSettings
	{
		ULSCombatDeveloperSettings* Settings;
		bool FriendlyFire;
		bool SelfDamage;
		~FRestoreSettings()
		{
			Settings->bAllowFriendlyFire = FriendlyFire;
			Settings->bAllowSelfDamage = SelfDamage;
		}
	} Restore{Settings, !!Settings->bAllowFriendlyFire, !!Settings->bAllowSelfDamage};
	Settings->bAllowFriendlyFire = false;
	Settings->bAllowSelfDamage = false;
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	ALSBasicAttackTestActor* Source = Play.GetTestWorld()->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Receiver = Play.GetTestWorld()->SpawnActor<ALSBasicAttackTestActor>();
	Receiver->HealthComponent->bDestroyOnDeath = false;
	if (!Play.BeginPlayInTestWorld())
		return false;
	FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(25, Source, Source);
	TestEqual(TEXT("일반 공격은 아군 피해 설정 유지"), Receiver->HealthComponent->ApplyDamage(Request).Status,
			  ELSApplyDamageStatus::RejectedFriendlyFireDisabled);
	TestEqual(TEXT("거부된 공격은 체력 보존"), Receiver->GetCurrentHealth(), 100.0f);
	Request.Policy = ELSDamagePolicy::EnvironmentalHazard;
	TestEqual(TEXT("환경 위험은 양진영 피해 허용"), Receiver->HealthComponent->ApplyDamage(Request).Status,
			  ELSApplyDamageStatus::Applied);
	TestEqual(TEXT("환경 위험의 실제 피해"), Receiver->GetCurrentHealth(), 75.0f);
	TestEqual(TEXT("공통 비치명 알림 유지"), Receiver->HitsReceived, 1);
	Request.DamageAmount = 75;
	Receiver->HealthComponent->ApplyDamage(Request);
	FLSHealthSnapshot Snapshot;
	TestTrue(TEXT("공통 사망 상태 조회"), Receiver->HealthComponent->TryGetHealth(Snapshot));
	TestTrue(TEXT("일반 사망 처리 경유"), Snapshot.bIsDead);
	TestEqual(TEXT("공개 사망 알림 한 번"), Receiver->HitsReceived, 2);
	Request = ULSHealthComponent::MakeDamageRequest(25, Source, Source);
	Request.Policy = ELSDamagePolicy::EnvironmentalHazard;
	TestEqual(TEXT("환경 위험도 별도 자기 피해 설정 유지"), Source->HealthComponent->ApplyDamage(Request).Status,
			  ELSApplyDamageStatus::RejectedSelfDamageDisabled);
	Play.ForwardErrorMessages(this);
	return true;
}

#endif
