#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSMobileCastle.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatSurfaceRangeTest, "Landship.Controls.SurfaceAttackRange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCombatSurfaceRangeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Source = World->SpawnActor<ALSTroopUnit>(FVector(300, 0, 96), FRotator::ZeroRotator);
	auto* Target = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 144), FRotator::ZeroRotator);
	Target->SetInitialFaction(ELSFaction::Enemy);
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	Source->Orders->SetComponentTickEnabled(false);
	FLSBasicAttackSettings Settings = Source->BasicAttack->GetAttackSettings();
	Settings.Range = 150;
	TestTrue(TEXT("경계 검사용 사거리 고정"), Source->BasicAttack->ConfigureAttack(Settings));
	Target->Orders->SetComponentTickEnabled(false);
	Source->GetCharacterMovement()->SetComponentTickEnabled(false);
	Target->GetCharacterMovement()->SetComponentTickEnabled(false);
	TestTrue(TEXT("중심 거리는 사거리 밖"),
	         FVector::Dist(Source->GetActorLocation(), Target->GetActorLocation()) > 150);
	TestTrue(TEXT("외곽 간 거리는 사거리 안"), ULSCombatTargetLibrary::GetAttackDistance(Source, Target) < 150);
	TestTrue(TEXT("큰 대상도 자동 탐색"), Source->BasicAttack->StartAutoAttack());
	TestTrue(TEXT("충돌 외곽을 사용하는 탐색 결과"), Source->BasicAttack->GetAttackTarget() == Target);
	FLSHealthSnapshot HP;
	Target->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(HP);
	TestEqual(TEXT("큰 대상 근접 타격"), HP.CurrentHealth, 988.0f);
	Source->SetActorLocation(FVector(400, 0, 96));
	for (int32 I = 0; I < 110; ++I)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	Target->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(HP);
	TestEqual(TEXT("외곽 사거리 밖에는 추가 피해 없음"), HP.CurrentHealth, 988.0f);
	Source->SetActorLocation(FVector(334, 0, 96));
	Source->BasicAttack->StartAutoAttack();
	Target->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(HP);
	TestEqual(TEXT("외곽 사거리 경계 포함"), HP.CurrentHealth, 976.0f);
	Source->SetActorLocation(FVector(0, 0, 1000));
	TestTrue(TEXT("수직으로 먼 대상을 근접 판정하지 않음"),
	         ULSCombatTargetLibrary::GetAttackDistance(Source, Target) > 150);
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif
