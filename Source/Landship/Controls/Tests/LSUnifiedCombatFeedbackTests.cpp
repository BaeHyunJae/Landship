#if WITH_DEV_AUTOMATION_TESTS
#include "Controls/LSRTSHUD.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSUnifiedCombatFeedbackTest, "Landship.Controls.CombatFeedback.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSUnifiedCombatFeedbackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Play.GetTestWorld();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World);
	if (!Play.BeginPlayInTestWorld()) return false;
	ULSHealthComponent* Health = Castle->FindComponentByClass<ULSHealthComponent>();
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10, nullptr, nullptr));
	// 같은 동기 테스트 안에서도 타이머가 매 프레임 한 번씩 진행되도록 한다.
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	ALSRTSHUD* HUD = World->SpawnActor<ALSRTSHUD>();
	TestTrue(TEXT("늦게 시작한 HUD도 이전 피해의 막대를 복원한다"), HUD->DamagedActors.Contains(Castle));
	TestTrue(TEXT("이 월드에서는 통합 피드백이 기존 피격 표시를 대체한다"), ALSRTSHUD::UsesUnifiedCombatFeedback(World));

	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Castle);
	Castle->AddInstanceComponent(Mesh);
	Mesh->SetupAttachment(Castle->GetRootComponent());
	Mesh->RegisterComponent();
	UMaterialInterface* Previous = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	Mesh->SetOverlayMaterial(Previous);
	TestTrue(TEXT("후속 피해가 실제 적용된다"), Health->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(20, nullptr, nullptr)).Status == ELSApplyDamageStatus::Applied);
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	HUD->Tick(0);
	const FLSCombatHealthDisplay* Display = HUD->DamagedActors.Find(Castle);
	TestTrue(TEXT("양수 피해는 감소 전후 체력 잔상을 보존한다"), Display && Display->BeforeFraction > Display->AfterFraction);
	TestTrue(TEXT("실제 메시의 흰 오버레이를 적용한다"), HUD->HitMaterial && Mesh->GetOverlayMaterial() == HUD->HitMaterial);
	TestTrue(TEXT("Hit overlay material supports skeletal mesh rendering"),
		HUD->HitMaterial && HUD->HitMaterial->GetUsageByFlag(MATUSAGE_SkeletalMesh));
	Castle->SetActorHiddenInGame(true);
	HUD->Tick(0);
	TestEqual(TEXT("시야에서 숨기면 이전 오버레이를 복원한다"), Mesh->GetOverlayMaterial(), Previous);
	TestTrue(TEXT("숨겨도 지속 체력 막대 상태는 남는다"), HUD->DamagedActors.Contains(Castle));
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.4f);
	Castle->SetActorHiddenInGame(false);
	HUD->Tick(0);
	TestTrue(TEXT("재등장해도 만료된 순간 효과를 재생하지 않는다"), HUD->HitOverlays.IsEmpty() && HUD->DamageFeedback.IsEmpty());

	ALSCastle* Spawned = LSCastleTestFixture::Spawn(World, FVector(1000, 0, 100));
	ULSHealthComponent* SpawnedHealth = Spawned->FindComponentByClass<ULSHealthComponent>();
	TestTrue(TEXT("HUD 뒤 생성한 액터에도 바인딩한다"), HUD->BoundHealth.Contains(TWeakObjectPtr<ULSHealthComponent>(SpawnedHealth)));
	TestTrue(TEXT("생성 뒤 피해가 실제 적용된다"), SpawnedHealth->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(15, nullptr, nullptr)).Status == ELSApplyDamageStatus::Applied);
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	HUD->Tick(0);
	TestTrue(TEXT("새 액터의 실제 피해도 막대를 만든다"), HUD->DamagedActors.Contains(Spawned));
	SpawnedHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10000, nullptr, nullptr));
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	HUD->Tick(0);
	TestFalse(TEXT("사망한 액터의 막대를 제거한다"), HUD->DamagedActors.Contains(Spawned));

	auto AdvanceFeedbackWorld = [&](float Seconds)
	{
		// 월드의 단일 프레임 시간 상한에 걸리지 않도록 나누되, 느린 프레임 확인은 0.2초로 유지한다.
		while (Seconds > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Seconds, 0.2f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			HUD->Tick(0);
			Seconds -= Step;
		}
	};
	TArray<FLSDamageResult> SameFrameDamage;
	for (const float Amount : {10.0f, 20.0f, 30.0f})
	{
		TestTrue(TEXT("같은 프레임의 피해를 실제로 적용한다"), Health->ApplyDamage(
			ULSHealthComponent::MakeDamageRequest(Amount, nullptr, nullptr)).Status == ELSApplyDamageStatus::Applied);
		FLSDamageResult Record;
		if (Health->TryGetLastDamage(Record)) SameFrameDamage.Add(Record);
	}
	AdvanceFeedbackWorld(0.01f);
	FLSHealthSnapshot Live;
	Health->TryGetHealth(Live);
	Display = HUD->DamagedActors.Find(Castle);
	TestTrue(TEXT("같은 프레임의 세 피해 뒤 막대 끝은 실제 체력과 같다"), Display &&
		FMath::IsNearlyEqual(Display->AfterFraction, Live.CurrentHealth / Live.MaxHealth));
	if (SameFrameDamage.Num() == 3)
	{
		HUD->OnDamageCommitted(SameFrameDamage[2]);
		HUD->OnDamageCommitted(SameFrameDamage[0]);
		HUD->OnDamageCommitted(SameFrameDamage[1]);
		HUD->Tick(0);
		Display = HUD->DamagedActors.Find(Castle);
		TestTrue(TEXT("기록이 순서 없이 도착해도 막대 끝은 이전 체력으로 돌아가지 않는다"), Display &&
			FMath::IsNearlyEqual(Display->AfterFraction, Live.CurrentHealth / Live.MaxHealth));
		TestTrue(TEXT("여러 피해의 감소 전 체력은 활성 잔상에 보존한다"), Display &&
			Display->BeforeFraction >= SameFrameDamage[0].PreviousHealth / SameFrameDamage[0].MaxHealth);
	}
	else AddError(TEXT("같은 프레임의 세 피해 기록이 필요하다"));
	const double BeforeExpiryAdvance = World->GetTimeSeconds();
	AdvanceFeedbackWorld(0.6f);
	TestTrue(TEXT("잔상 만료 검사는 월드 시간을 실제로 0.6초 진행한다"),
		World->GetTimeSeconds() - BeforeExpiryAdvance >= 0.59);
	Display = HUD->DamagedActors.Find(Castle);
	TestTrue(TEXT("순서 없는 알림 뒤에도 잔상이 끝나면 실제 체력까지 줄어든다"), Display &&
		FMath::IsNearlyEqual(Display->GetGhostFraction(World->GetTimeSeconds()), Live.CurrentHealth / Live.MaxHealth));

	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(5, nullptr, nullptr));
	FLSDamageResult SlowFrameDamage;
	Health->TryGetLastDamage(SlowFrameDamage);
	AdvanceFeedbackWorld(0.2f);
	TestTrue(TEXT("느린 프레임에서도 수신 시점부터 흰 피격 표시를 시작한다"),
		Mesh->GetOverlayMaterial() == HUD->HitMaterial && !HUD->HitOverlays.IsEmpty() &&
		HUD->HitOverlays[0].ExpiresAt >= World->GetTimeSeconds() + 0.09);
	TestTrue(TEXT("표시 시간이 피해 기록의 원래 시각을 바꾸지 않는다"), !HUD->DamageFeedback.IsEmpty() &&
		HUD->DamageFeedback.Last().Damage.TimestampSeconds == SlowFrameDamage.TimestampSeconds);
	AdvanceFeedbackWorld(0.4f);
	Castle->SetActorHiddenInGame(true);
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(5, nullptr, nullptr));
	Castle->SetActorHiddenInGame(false);
	AdvanceFeedbackWorld(0.2f);
	TestTrue(TEXT("피해 확정 때 숨겨졌다면 다음 틱에 보여도 피격 표시를 재생하지 않는다"),
		HUD->HitOverlays.IsEmpty() && HUD->DamageFeedback.IsEmpty());

	const FTransform MeleeSourceTransform(FVector(3000, 0, 90));
	ALSBasicAttackTestActor* MeleeSource = World->SpawnActorDeferred<ALSBasicAttackTestActor>(
		ALSBasicAttackTestActor::StaticClass(), MeleeSourceTransform);
	FLSBasicAttackSettings MeleeSettings;
	MeleeSettings.Damage = 1;
	MeleeSettings.Range = 100;
	MeleeSettings.Interval = 10;
	MeleeSource->Attack->ConfigureAttack(MeleeSettings);
	MeleeSource->FinishSpawning(MeleeSourceTransform);
	const FTransform MeleeTargetTransform(FVector(3040, 0, 90));
	ALSBasicAttackTestActor* MeleeTarget = World->SpawnActorDeferred<ALSBasicAttackTestActor>(
		ALSBasicAttackTestActor::StaticClass(), MeleeTargetTransform);
	MeleeTarget->SetInitialFaction(ELSFaction::Enemy);
	MeleeTarget->FinishSpawning(MeleeTargetTransform);
	const double MeleeExecutionTime = World->GetTimeSeconds();
	TestTrue(TEXT("느린 프레임 검사에 실제 근접 공격을 확정한다"), MeleeSource->Attack->StartAttack(MeleeTarget));
	MeleeSource->Attack->StopAttack();
	AdvanceFeedbackWorld(0.2f);
	TestTrue(TEXT("근접 방향선은 0.2초 프레임 뒤에도 수신 시점부터 표시한다"),
		!HUD->AttackFeedback.IsEmpty() && HUD->AttackFeedback.Last().ReceivedAt >= MeleeExecutionTime + 0.19 &&
		HUD->AttackFeedback.Last().Execution.TimestampSeconds == MeleeExecutionTime);
	AdvanceFeedbackWorld(0.2f);
	TestTrue(TEXT("실제 표시 시간을 지난 근접 방향선은 다음 프레임에 끝난다"), HUD->AttackFeedback.IsEmpty());

	int32 AfterShutdownDispatches = 0;
	const FDelegateHandle ShutdownObserver = Health->OnDamageCommitted.AddLambda(
		[&AfterShutdownDispatches](const FLSDamageResult&) { ++AfterShutdownDispatches; });
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(5, nullptr, nullptr));
	HUD->EndPlay(EEndPlayReason::RemovedFromWorld);
	AdvanceFeedbackWorld(0.01f);
	TestEqual(TEXT("종료 전에 복사한 피해 알림이 실제로 지연 전달된다"), AfterShutdownDispatches, 1);
	Health->OnDamageCommitted.Remove(ShutdownObserver);
	TestTrue(TEXT("종료 뒤 복사된 피해 구독이 도착해도 대기 기록을 다시 만들지 않는다"),
		HUD->PendingDamage.IsEmpty() && HUD->PendingAttacks.IsEmpty() && HUD->DamagedActors.IsEmpty());
	TestTrue(TEXT("종료 뒤 늦은 알림이 흰 오버레이를 되살리지 않는다"),
		HUD->HitOverlays.IsEmpty() && HUD->DamageFeedback.IsEmpty() && Mesh->GetOverlayMaterial() == Previous);
	return true;
}
#endif
