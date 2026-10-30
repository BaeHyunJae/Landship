#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Tests/LSHealthTestObserver.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include <limits>

namespace
{
ULSHealthComponent* AddHealth(AActor* Owner)
{
	ULSHealthComponent* Health = NewObject<ULSHealthComponent>(Owner);
	Owner->AddInstanceComponent(Health);
	Health->RegisterComponent();
	return Health;
}

bool Initialize(ULSHealthComponent* Health, FLSPrepareCombatDeath PrepareDeathEvent = FLSPrepareCombatDeath::CreateLambda([] {}))
{
	return Health->InitializeHealth(ELSCombatStatsInitResult::Success, 100.0f, PrepareDeathEvent, FString());
}

FLSDamageRequest Request(float Amount, AActor* Source = nullptr, AActor* Causer = nullptr)
{
	FLSDamageRequest Damage;
	Damage.DamageAmount = Amount;
	Damage.SourceActor = Source;
	Damage.DamageCauser = Causer;
	return Damage;
}

ULSHealthTestObserver* Observe(ULSHealthComponent* Health)
{
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(Health);
	Health->OnDamagedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDamage);
	Health->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	return Observer;
}

struct FLSScopedCombatDamageSettings
{
	bool bOriginalSelfDamage = GetDefault<ULSCombatDeveloperSettings>()->bAllowSelfDamage;
	bool bOriginalFriendlyFire = GetDefault<ULSCombatDeveloperSettings>()->bAllowFriendlyFire;
	~FLSScopedCombatDamageSettings()
	{
		ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthDamageTest, "Landship.Combat.Health.DamageAndNotifications",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthDamageTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	AActor* Owner = World.GetTestWorld()->SpawnActor<AActor>();
	AActor* Source = World.GetTestWorld()->SpawnActor<AActor>();
	AActor* Causer = World.GetTestWorld()->SpawnActor<AActor>();
	ULSHealthComponent* Health = AddHealth(Owner);
	FLSHealthSnapshot Snapshot;
	Snapshot.CurrentHealth = 4321.0f;
	Snapshot.MaxHealth = 9876.0f;
	Snapshot.bIsDead = true;
	TestFalse(TEXT("초기화 전 조회 거부"), Health->TryGetHealth(Snapshot));
	TestEqual(TEXT("실패 조회 현재 체력 보존"), Snapshot.CurrentHealth, 4321.0f);
	TestEqual(TEXT("실패 조회 최대 체력 보존"), Snapshot.MaxHealth, 9876.0f);
	TestTrue(TEXT("실패 조회 사망값 보존"), !!Snapshot.bIsDead);
	bool bPrepared = false;
	TestTrue(TEXT("초기화 성공"), Initialize(Health, FLSPrepareCombatDeath::CreateLambda([&] { bPrepared = true; })));
	ULSHealthTestObserver* Observer = Observe(Health);
	Observer->DeathCallback = [&](const FLSDamageResult& Damage)
	{
		TestTrue(TEXT("공개 사망 알림 전 네이티브 훅 완료"), bPrepared);
		TestTrue(TEXT("공개 알림에서 체력 조회 가능"), Health->TryGetHealth(Snapshot));
		TestTrue(TEXT("공개 알림에서 이미 사망"), !!Snapshot.bIsDead);
		TestEqual(TEXT("공개 알림에서 이미 체력 0"), Snapshot.CurrentHealth, 0.0f);
	};
	const FLSApplyDamageResult Nonlethal = Health->ApplyDamage(Request(30.0f, Source, Causer));
	TestEqual(TEXT("비치명 적용 상태"), Nonlethal.Status, ELSApplyDamageStatus::Applied);
	TestEqual(TEXT("비치명 실제 감소량"), Nonlethal.AppliedDamage, 30.0f);
	TestEqual(TEXT("비치명 알림 한 번"), Observer->DamageResults.Num(), 1);
	TestEqual(TEXT("비치명에는 사망 알림 없음"), Observer->DeathResults.Num(), 0);
	if (Observer->DamageResults.Num() == 1)
	{
		const FLSDamageResult& Damage = Observer->DamageResults[0];
		TestEqual(TEXT("알림 요청량"), Damage.RequestedDamage, 30.0f);
		TestEqual(TEXT("알림 감소량"), Damage.AppliedDamage, 30.0f);
		TestEqual(TEXT("알림 체력"), Damage.CurrentHealth, 70.0f);
		TestTrue(TEXT("알림 공격 주체 보존"), Damage.SourceActor == Source);
		TestTrue(TEXT("알림 직접 원인 보존"), Damage.DamageCauser == Causer);
	}
	const FLSApplyDamageResult Lethal = Health->ApplyDamage(Request(200.0f, Source, Causer));
	TestEqual(TEXT("초과 피해는 남은 체력만 감소"), Lethal.AppliedDamage, 70.0f);
	TestEqual(TEXT("치명에는 피격 알림 추가 없음"), Observer->DamageResults.Num(), 1);
	TestEqual(TEXT("치명 사망 알림 한 번"), Observer->DeathResults.Num(), 1);
	if (Observer->DeathResults.Num() == 1)
	{
		TestEqual(TEXT("치명 알림 요청량 보존"), Observer->DeathResults[0].RequestedDamage, 200.0f);
		TestEqual(TEXT("치명 알림 실제 감소량"), Observer->DeathResults[0].AppliedDamage, 70.0f);
	}
	TestEqual(TEXT("사망 후 재피격 거부"), Health->ApplyDamage(Request(1)).Status, ELSApplyDamageStatus::RejectedDead);
	TestEqual(TEXT("사망 알림 중복 없음"), Observer->DeathResults.Num(), 1);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthReentrantTest, "Landship.Combat.Health.ReentrancyAndSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthReentrantTest::RunTest(const FString& Parameters)
{
	FLSScopedCombatDamageSettings RestoreSettings;
	GetMutableDefault<ULSCombatDeveloperSettings>()->bAllowFriendlyFire = true;
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	ALSHealthTestActor* Source = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
	ALSHealthTestActor* Target = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
	ULSHealthComponent* SourceHealth = AddHealth(Source);
	ULSHealthComponent* Health = AddHealth(Target);
	TestTrue(TEXT("주체 준비"), Initialize(SourceHealth));
	TestTrue(TEXT("대상 준비"), Initialize(Health));
	TestFalse(TEXT("동일 진영 자동 표적 거부"), ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Target));
	Source->Faction = ELSFaction::Enemy;
	TestTrue(TEXT("다른 진영 자동 표적 허용"), ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Target));
	Source->Faction = ELSFaction::Ally;
	TestEqual(TEXT("동일 진영 명중 피해 허용"), Health->ApplyDamage(Request(10, Source)).AppliedDamage, 10.0f);
	SourceHealth->ApplyDamage(Request(100));
	Source->Faction = ELSFaction::Enemy;
	TestFalse(TEXT("사망 주체 자동 표적 거부"), ULSCombatTargetLibrary::CanAutoAttackTarget(Source, Target));
	TestEqual(TEXT("사망한 주체의 이미 발사된 피해 허용"), Health->ApplyDamage(Request(10, Source)).AppliedDamage, 10.0f);
	ULSHealthTestObserver* Observer = Observe(Health);
	Observer->DamageCallback = [&](const FLSDamageResult& Damage)
	{
		TestEqual(TEXT("외부 요청 알림 당시 체력"), Damage.CurrentHealth, 60.0f);
		TestEqual(TEXT("알림 중 치명 요청의 실제 감소량"), Health->ApplyDamage(Request(999)).AppliedDamage, 60.0f);
	};
	const FLSApplyDamageResult Outer = Health->ApplyDamage(Request(20));
	TestEqual(TEXT("재진입 후에도 원래 요청의 감소량 반환"), Outer.AppliedDamage, 20.0f);
	TestEqual(TEXT("원래 요청 피격 한 번"), Observer->DamageResults.Num(), 1);
	TestEqual(TEXT("재진입 사망 한 번"), Observer->DeathResults.Num(), 1);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthInputTest, "Landship.Combat.Health.InputAndSelfDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthInputTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	AActor* Owner = World.GetTestWorld()->SpawnActor<AActor>();
	ULSHealthComponent* Health = AddHealth(Owner);
	TestTrue(TEXT("초기화"), Initialize(Health));
	ULSHealthTestObserver* Observer = Observe(Health);
	FLSScopedCombatDamageSettings RestoreSettings;
	GetMutableDefault<ULSCombatDeveloperSettings>()->bAllowSelfDamage = false;
	TestEqual(TEXT("자기 피해 설정 꺼짐"), Health->ApplyDamage(Request(20, Owner)).Status, ELSApplyDamageStatus::RejectedSelfDamageDisabled);
	GetMutableDefault<ULSCombatDeveloperSettings>()->bAllowSelfDamage = true;
	TestEqual(TEXT("같은 인스턴스에 변경된 자기 피해 설정 반영"), Health->ApplyDamage(Request(20, Owner)).AppliedDamage, 20.0f);
	TestEqual(TEXT("0 피해 정상 무효"), Health->ApplyDamage(Request(0)).Status, ELSApplyDamageStatus::NoEffectZeroDamage);
	const FString Prefix = TEXT("[LSHealthComponent::ApplyDamage] Invalid damage:");
	AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 3);
	FEnsureScope EnsureScope([&](const FEnsureHandlerArgs& Args)
	{
		if (!FString(Args.Message).StartsWith(Prefix)) return false;
		AddError(Args.Message);
		return true;
	});
	for (float Amount : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		const FLSApplyDamageResult Result = Health->ApplyDamage(Request(Amount));
		TestEqual(TEXT("잘못된 피해 상태"), Result.Status, ELSApplyDamageStatus::RejectedInvalidInput);
		TestEqual(TEXT("잘못된 피해 감소 없음"), Result.AppliedDamage, 0.0f);
	}
	TestEqual(TEXT("실제 입력 ensure 세 번"), EnsureScope.GetCount(), 3);
	FLSHealthSnapshot Snapshot;
	TestTrue(TEXT("체력 조회"), Health->TryGetHealth(Snapshot));
	TestEqual(TEXT("거부·0 피해는 체력 유지"), Snapshot.CurrentHealth, 80.0f);
	TestEqual(TEXT("거부·0 피해는 알림 없음"), Observer->DamageResults.Num(), 1);
	TestEqual(TEXT("사망 알림 없음"), Observer->DeathResults.Num(), 0);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthInitializationTest, "Landship.Combat.Health.InitializationFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthInitializationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	const FString Prefix = TEXT("[LSHealthComponent::");
	AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 11);
	FEnsureScope EnsureScope([&](const FEnsureHandlerArgs& Args)
	{
		if (!FString(Args.Message).StartsWith(Prefix)) return false;
		AddError(Args.Message);
		return true;
	});
	ULSHealthComponent* Health = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	TestEqual(TEXT("미준비 실행 상태"), Health->ApplyDamage(Request(1)).Status, ELSApplyDamageStatus::RejectedNotReady);
	TestFalse(TEXT("수치 실패는 준비 실패"), Health->InitializeHealth(ELSCombatStatsInitResult::MissingTable, 100, FLSPrepareCombatDeath(), FString()));
	TestEqual(TEXT("수치 실패를 중복 진단하지 않음"), EnsureScope.GetCount(), 1);
	TestEqual(TEXT("실패 실행 상태 구분"), Health->ApplyDamage(Request(1)).Status, ELSApplyDamageStatus::RejectedInitializationFailed);
	TestFalse(TEXT("실패 후 재초기화 거부"), Initialize(Health));
	FLSHealthSnapshot Snapshot;
	Snapshot.CurrentHealth = 1234;
	TestFalse(TEXT("실패 조회 거부"), Health->TryGetHealth(Snapshot));
	TestEqual(TEXT("실패 출력 보존"), Snapshot.CurrentHealth, 1234.0f);
	TestFalse(TEXT("실패 원인 보관"), Health->GetInitializationFailureReason().IsEmpty());
	ULSHealthComponent* Ready = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	TestTrue(TEXT("정상 첫 초기화"), Initialize(Ready));
	TestFalse(TEXT("성공 후 재초기화 거부"), Initialize(Ready));
	TestTrue(TEXT("재초기화 거부 후 조회"), Ready->TryGetHealth(Snapshot));
	TestEqual(TEXT("재초기화 거부 후 체력 유지"), Snapshot.CurrentHealth, 100.0f);
	ULSHealthComponent* Duplicate = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	AddHealth(Duplicate->GetOwner());
	TestFalse(TEXT("중복 컴포넌트 준비 거부"), Initialize(Duplicate));
	ULSHealthComponent* MissingHook = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	TestFalse(TEXT("네이티브 훅 누락 거부"), Initialize(MissingHook, FLSPrepareCombatDeath()));
	for (float Delay : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		ULSHealthComponent* Invalid = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
		Invalid->DestroyDelay = Delay;
#if WITH_EDITOR
		FDataValidationContext Validation;
		TestEqual(TEXT("에디터 제거 지연 검증"), Invalid->IsDataValid(Validation), EDataValidationResult::Invalid);
#endif
		TestFalse(TEXT("런타임 제거 지연 검증"), Initialize(Invalid));
	}
	ULSHealthComponent* InvalidInitial = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	TestFalse(TEXT("0 초기 체력 거부"), InvalidInitial->InitializeHealth(ELSCombatStatsInitResult::Success, 0, FLSPrepareCombatDeath::CreateLambda([] {}), FString()));
	ULSHealthComponent* OwnerError = AddHealth(World.GetTestWorld()->SpawnActor<AActor>());
	TestFalse(TEXT("소유자 구성 오류 거부"), OwnerError->InitializeHealth(ELSCombatStatsInitResult::Success, 100, FLSPrepareCombatDeath::CreateLambda([] {}), TEXT("Invalid owner configuration")));
	TestEqual(TEXT("실제 준비 오류 ensure 횟수"), EnsureScope.GetCount(), 11);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthRemovalTest, "Landship.Combat.Health.RemovalAfterNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthRemovalTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	if (!TestTrue(TEXT("게임 시작"), World.BeginPlayInTestWorld())) return false;
	for (int32 Mode = 0; Mode < 4; ++Mode)
	{
		AActor* Owner = World.GetTestWorld()->SpawnActor<AActor>();
		ULSHealthComponent* Health = AddHealth(Owner);
		Health->bDestroyOnDeath = Mode != 0;
		Health->DestroyDelay = Mode == 1 ? 0.0f : 0.25f;
		TestTrue(TEXT("초기화"), Initialize(Health));
		ULSHealthTestObserver* Observer = Observe(Health);
		Observer->DeathCallback = [&](const FLSDamageResult& Damage)
		{
			TestFalse(TEXT("사망 알림 중 자동 제거 전"), Owner->IsActorBeingDestroyed());
			TestEqual(TEXT("사망 알림 중 제거 타이머 없음"), Owner->GetLifeSpan(), 0.0f);
			if (Mode == 3) Owner->Destroy();
		};
		TestEqual(TEXT("제거 여부와 관계없이 실제 피해 반환"), Health->ApplyDamage(Request(100)).AppliedDamage, 100.0f);
		TestEqual(TEXT("사망 알림 한 번"), Observer->DeathResults.Num(), 1);
		if (Mode == 0)
		{
			TestFalse(TEXT("제거 끄면 보존"), Owner->IsActorBeingDestroyed());
			TestEqual(TEXT("보존 대상 타이머 없음"), Owner->GetLifeSpan(), 0.0f);
		}
		else if (Mode == 2)
		{
			TestFalse(TEXT("양수 지연 즉시 제거 없음"), Owner->IsActorBeingDestroyed());
			TestTrue(TEXT("양수 지연 제거 예약"), Owner->GetLifeSpan() > 0.0f);
			// 첫 프레임 끝에 대기 타이머가 활성화되므로 다음 프레임부터 지연을 경과시킨다.
			TestTrue(TEXT("제거 타이머 활성화 프레임"), World.TickTestWorld(0.01f));
			TestFalse(TEXT("지연 전에 대상 유지"), Owner->IsActorBeingDestroyed());
			TestTrue(TEXT("제거 지연을 경과시키는 프레임"), World.TickTestWorld(0.5f));
			TestTrue(TEXT("지연 경과 후 실제 제거 요청"), Owner->IsActorBeingDestroyed());
		}
		else
		{
			TestTrue(TEXT("0 지연 또는 구독자 제거는 반환 전에 제거 요청"), Owner->IsActorBeingDestroyed());
			TestEqual(TEXT("제거 후 새 타이머 없음"), Owner->GetLifeSpan(), 0.0f);
		}
	}
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthFriendlyFireTest, "Landship.Combat.Health.FriendlyFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthFriendlyFireTest::RunTest(const FString& Parameters)
{
	FLSScopedCombatDamageSettings RestoreSettings;
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	if (!TestTrue(TEXT("게임 시작"), World.BeginPlayInTestWorld())) return false;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	{
		for (bool bAllowSelfDamage : {false, true})
		{
			for (bool bAllowFriendlyFire : {false, true})
			{
				Settings->bAllowSelfDamage = bAllowSelfDamage;
				Settings->bAllowFriendlyFire = bAllowFriendlyFire;
				// 자기 자신·동일 진영의 타인·상대 진영에 두 설정의 네 조합을 각각 적용한다.
				for (int32 SourceKind = 0; SourceKind < 3; ++SourceKind)
				{
					ALSHealthTestActor* Target = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
					Target->Faction = Faction;
					ALSHealthTestActor* Source = SourceKind == 0 ? Target : World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
					Source->Faction = SourceKind == 2
						? (Faction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally) : Faction;
					ULSHealthComponent* Health = AddHealth(Target);
					TestTrue(TEXT("진영 조합별 체력 초기화"), Initialize(Health));
					ULSHealthTestObserver* Observer = Observe(Health);
					const bool bAllowed = SourceKind == 0 ? bAllowSelfDamage : SourceKind == 2 || bAllowFriendlyFire;
					const ELSApplyDamageStatus ExpectedStatus = bAllowed ? ELSApplyDamageStatus::Applied
						: SourceKind == 0 ? ELSApplyDamageStatus::RejectedSelfDamageDisabled : ELSApplyDamageStatus::RejectedFriendlyFireDisabled;
					const FString Case = FString::Printf(TEXT("진영=%d 자기피해=%d 오폭=%d 주체종류=%d"),
						static_cast<int32>(Faction), bAllowSelfDamage, bAllowFriendlyFire, SourceKind);
					const FLSApplyDamageResult Result = Health->ApplyDamage(Request(10.0f, Source));
					TestEqual(Case + TEXT(" 상태"), Result.Status, ExpectedStatus);
					TestEqual(Case + TEXT(" 실제 감소량"), Result.AppliedDamage, bAllowed ? 10.0f : 0.0f);
					if (!bAllowed)
					{
						const FLSApplyDamageResult Lethal = Health->ApplyDamage(Request(1000.0f, Source));
						TestEqual(Case + TEXT(" 치명 요청도 거부"), Lethal.Status, ExpectedStatus);
						TestEqual(Case + TEXT(" 치명 요청 감소 없음"), Lethal.AppliedDamage, 0.0f);
					}
					FLSHealthSnapshot Snapshot;
					TestTrue(Case + TEXT(" 체력 조회"), Health->TryGetHealth(Snapshot));
					TestEqual(Case + TEXT(" 현재 체력"), Snapshot.CurrentHealth, bAllowed ? 90.0f : 100.0f);
					TestFalse(Case + TEXT(" 생존 유지"), !!Snapshot.bIsDead);
					TestEqual(Case + TEXT(" 피격 알림"), Observer->DamageResults.Num(), bAllowed ? 1 : 0);
					TestEqual(Case + TEXT(" 사망 알림 없음"), Observer->DeathResults.Num(), 0);
				}
			}
		}

		Settings->bAllowSelfDamage = false;
		Settings->bAllowFriendlyFire = false;
		ALSHealthTestActor* Target = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
		ALSHealthTestActor* SameFactionSource = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
		ALSHealthTestActor* OpponentSource = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
		Target->Faction = SameFactionSource->Faction = Faction;
		OpponentSource->Faction = Faction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally;
		ULSHealthComponent* Health = AddHealth(Target);
		TestTrue(TEXT("지연 공격 대상 초기화"), Initialize(Health));
		ULSHealthTestObserver* Observer = Observe(Health);
		const FLSDamageRequest CapturedFriendly = ULSHealthComponent::MakeDamageRequest(10.0f, SameFactionSource, nullptr);
		const FLSDamageRequest CapturedEnemy = ULSHealthComponent::MakeDamageRequest(10.0f, OpponentSource, nullptr);
		TestEqual(TEXT("기존 직접 요청도 생존 주체 진영으로 오폭 거부"),
			Health->ApplyDamage(Request(10.0f, SameFactionSource)).Status, ELSApplyDamageStatus::RejectedFriendlyFireDisabled);
		Settings->bAllowFriendlyFire = true;
		TestEqual(TEXT("같은 컴포넌트의 다음 요청부터 오폭 허용"),
			Health->ApplyDamage(Request(10.0f, SameFactionSource)).AppliedDamage, 10.0f);
		Settings->bAllowFriendlyFire = false;
		TestEqual(TEXT("같은 컴포넌트에서 다시 오폭 차단"),
			Health->ApplyDamage(Request(10.0f, SameFactionSource)).Status, ELSApplyDamageStatus::RejectedFriendlyFireDisabled);
		TestTrue(TEXT("같은 진영 공격 주체 제거"), SameFactionSource->Destroy());
		TestTrue(TEXT("상대 진영 공격 주체 제거"), OpponentSource->Destroy());
		TestFalse(TEXT("같은 진영 주체가 실제 무효화됨"), IsValid(SameFactionSource));
		TestFalse(TEXT("상대 진영 주체가 실제 무효화됨"), IsValid(OpponentSource));
		TestEqual(TEXT("제거된 같은 진영 주체의 저장된 요청 차단"),
			Health->ApplyDamage(CapturedFriendly).Status, ELSApplyDamageStatus::RejectedFriendlyFireDisabled);
		TestEqual(TEXT("제거된 상대 진영 주체의 저장된 요청 허용"),
			Health->ApplyDamage(CapturedEnemy).AppliedDamage, 10.0f);
		AActor* Environment = World.GetTestWorld()->SpawnActor<AActor>();
		TestEqual(TEXT("공격 주체가 없는 환경 피해 허용"),
			Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10.0f, nullptr, Environment)).AppliedDamage, 10.0f);
		TestEqual(TEXT("공격 주체와 직접 원인이 모두 없는 피해 허용"),
			Health->ApplyDamage(Request(10.0f)).AppliedDamage, 10.0f);
		TestEqual(TEXT("진영 인터페이스가 없는 주체의 피해 허용"),
			Health->ApplyDamage(Request(10.0f, Environment)).AppliedDamage, 10.0f);
		FLSHealthSnapshot Snapshot;
		TestTrue(TEXT("설정 변경·주체 제거 후 체력 조회"), Health->TryGetHealth(Snapshot));
		TestEqual(TEXT("허용된 다섯 요청만 체력 감소"), Snapshot.CurrentHealth, 50.0f);
		TestEqual(TEXT("차단 요청은 피격 알림 없음"), Observer->DamageResults.Num(), 5);
		TestEqual(TEXT("차단 요청은 사망 알림 없음"), Observer->DeathResults.Num(), 0);
		if (Observer->DamageResults.Num() == 5)
		{
			TestTrue(TEXT("주체 제거 후 알림은 유효하지 않은 주체를 보관하지 않음"), Observer->DamageResults[1].SourceActor == nullptr);
			TestTrue(TEXT("환경 피해의 직접 원인 보존"), Observer->DamageResults[2].DamageCauser == Environment);
		}
	}
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSHealthDestroyedReceiverReentrancyTest,
	"Landship.Combat.Health.DestroyedReceiverReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSHealthDestroyedReceiverReentrancyTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	if (!TestTrue(TEXT("게임 시작"), World.BeginPlayInTestWorld())) return false;
	ALSHealthTestActor* Owner = World.GetTestWorld()->SpawnActor<ALSHealthTestActor>();
	if (!TestNotNull(TEXT("소유 액터 생성"), Owner)) return false;
	ULSHealthComponent* Health = AddHealth(Owner);
	TestTrue(TEXT("실제 소유자에 연결한 네이티브 사망 훅으로 초기화"),
		Initialize(Health, FLSPrepareCombatDeath::CreateUObject(Owner, &ALSHealthTestActor::PrepareForTestDeath)));
	ULSHealthTestObserver* Observer = Observe(Health);
	FLSApplyDamageResult Nested;
	Observer->DamageCallback = [&](const FLSDamageResult& Damage)
	{
		TestTrue(TEXT("비치명 구독자에서 소유자 제거"), Owner->Destroy());
		TestFalse(TEXT("제거된 액터는 이미 무효"), IsValid(Owner));
		TestFalse(TEXT("제거된 액터의 체력 컴포넌트도 이미 무효"), IsValid(Health));
		// GC 전 동기 콜백에 남은 포인터로 재진입하여 무효 수신자 거부 경로를 확인한다.
		Nested = Health->ApplyDamage(Request(999.0f));
	};
	const FLSApplyDamageResult Outer = Health->ApplyDamage(Request(20.0f));
	TestEqual(TEXT("원래 요청 적용 상태 보존"), Outer.Status, ELSApplyDamageStatus::Applied);
	TestEqual(TEXT("원래 요청 감소량 보존"), Outer.AppliedDamage, 20.0f);
	TestEqual(TEXT("제거된 수신자 재진입 거부"), Nested.Status, ELSApplyDamageStatus::RejectedInvalidTarget);
	TestEqual(TEXT("거부된 재진입 감소량 없음"), Nested.AppliedDamage, 0.0f);
	TestEqual(TEXT("무효 소유자의 네이티브 사망 훅 실행 없음"), Owner->NativeDeathCount, 0);
	TestEqual(TEXT("원래 비치명 알림만 한 번"), Observer->DamageResults.Num(), 1);
	TestEqual(TEXT("공개 사망 알림 없음"), Observer->DeathResults.Num(), 0);
	FLSHealthSnapshot Snapshot;
	TestTrue(TEXT("GC 전 저장된 체력 조회"), Health->TryGetHealth(Snapshot));
	TestEqual(TEXT("재진입으로 두 번째 체력 감소 없음"), Snapshot.CurrentHealth, 80.0f);
	TestFalse(TEXT("재진입으로 사망 상태 변경 없음"), !!Snapshot.bIsDead);
	World.ForwardErrorMessages(this);
	return true;
}

#endif
