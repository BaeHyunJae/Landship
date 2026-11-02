#if WITH_DEV_AUTOMATION_TESTS

#include "Troops/LSTroopUnit.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Combat/LSHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopAnimationTest, "Landship.Troops.AnimationBindings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopAnimationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Play.GetTestWorld();
	TArray<ALSTroopUnit*> Units;
	TArray<ALSTroopUnit*> Enemies;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Unit = World->SpawnActor<ALSTroopUnit>(FVector(0, Index * 600, 100), FRotator::ZeroRotator, Params);
		auto* Enemy = World->SpawnActor<ALSTroopUnit>(FVector(180, Index * 600, 100), FRotator::ZeroRotator, Params);
		if (!Unit || !Enemy)
			return false;
		Unit->SetInitialTroopType(static_cast<ELSTroopType>(Index));
		Enemy->SetInitialFaction(ELSFaction::Enemy);
		Units.Add(Unit);
		Enemies.Add(Enemy);
	}
	if (!Play.BeginPlayInTestWorld())
		return false;
	for (auto* Actor : TArray<ALSTroopUnit*>{Units[0], Units[1], Units[2], Enemies[0], Enemies[1], Enemies[2]})
	{
		Actor->Orders->SetComponentTickEnabled(false);
		Actor->GetCharacterMovement()->DisableMovement();
		Actor->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		auto* Unit = Units[Index];
		auto* Instance = Unit->GetMesh()->GetSingleNodeInstance();
		if (!TestNotNull(TEXT("실제 단일 애니메이션 인스턴스 생성"), Instance))
			return false;
		TestEqual(TEXT("병종별 대기 클립 연결"), Instance->GetCurrentAsset()->GetFName(),
		          FName(Index == 1 ? TEXT("A_Enemy_Idle") : TEXT("A_2HandIdle")));
		TestTrue(TEXT("대기 반복 재생"), Instance->IsLooping());
		Unit->SetActorRotation(FRotator(0, 180, 0));
		TestTrue(TEXT("실제 공격 요청 접수"), Unit->BasicAttack->StartAttack(Enemies[Index]));
		Unit->BasicAttack->StopAttack();
		TestEqual(TEXT("피해 명중 대신 공격 실행에 맞춰 동작 재생"), Instance->GetCurrentAsset()->GetFName(),
		          FName(Index == 1 ? TEXT("A_Enemy_Shoot") : TEXT("A_Parry_BaseballHit")));
		TestFalse(TEXT("공격은 한 번만 재생"), Instance->IsLooping());
		const FVector Direction = (Enemies[Index]->GetActorLocation() - Unit->GetActorLocation()).GetSafeNormal2D();
		TestTrue(TEXT("공격 실행 시 표적을 즉시 바라봄"),
		         FVector::DotProduct(Unit->GetActorForwardVector(), Direction) > 0.99f);
	}
	++GFrameCounter;
	World->Tick(LEVELTICK_All, 0.01f);
	for (int32 Index = 0; Index < Units.Num(); ++Index)
		TestEqual(TEXT("공격 취소 다음 프레임에 남은 모션을 정리"),
		          Units[Index]->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),
		          FName(Index == 1 ? TEXT("A_Enemy_Idle") : TEXT("A_2HandIdle")));
	FLSHealthSnapshot Before;
	Enemies[0]->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Before);
	Units[0]->BasicAttack->StartAttack(Enemies[0]);
	FLSHealthSnapshot After;
	Enemies[0]->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(After);
	TestEqual(TEXT("공격 취소·재요청이 기존 쿨타임을 초기화하지 않음"), After.CurrentHealth, Before.CurrentHealth);
	Units[0]->BasicAttack->StopAttack();
	auto* Marker = Cast<UMaterialInstanceDynamic>(Units[0]->GetTroopMarker()->GetMaterial(0));
	if (!TestNotNull(TEXT("피격 표식 머티리얼"), Marker))
		return false;
	const FLinearColor NormalColor = Marker->K2_GetVectorParameterValue(TEXT("TeamColor"));
	Units[0]->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
	    ULSHealthComponent::MakeDamageRequest(1, Enemies[0], Enemies[0]));
	TestFalse(TEXT("비치명 피해 직후 표식 강조"),
	          Marker->K2_GetVectorParameterValue(TEXT("TeamColor")).Equals(NormalColor));
	for (int32 Step = 0; Step < 110; ++Step)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	for (int32 Index = 0; Index < Units.Num(); ++Index)
		TestEqual(TEXT("공격 완료 후 원래 대기 동작 복귀"),
		          Units[Index]->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),
		          FName(Index == 1 ? TEXT("A_Enemy_Idle") : TEXT("A_2HandIdle")));
	TestTrue(TEXT("강조 종료 후 진영 색 복원"),
	         Marker->K2_GetVectorParameterValue(TEXT("TeamColor")).Equals(NormalColor));
	Units[0]->BasicAttack->StartAttack(Enemies[0]);
	Units[0]->BasicAttack->StopAttack();
	TestEqual(TEXT("사망 검사 전에 새 공격 동작 재생 확인"),
	          Units[0]->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),
	          FName(TEXT("A_Parry_BaseballHit")));
	const auto Request = ULSHealthComponent::MakeDamageRequest(10000, Enemies[0], Enemies[0]);
	Units[0]->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(Request);
	for (int32 Step = 0; Step < 70; ++Step)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	TestTrue(TEXT("공격 도중 사망해도 대기 복귀가 랙돌을 덮어쓰지 않음"), Units[0]->GetMesh()->IsSimulatingPhysics());
	TestEqual(TEXT("사망 후 대기 재생 금지"),
	          Units[0]->GetMesh()->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),
	          FName(TEXT("A_Parry_BaseballHit")));
	return true;
}
#endif
