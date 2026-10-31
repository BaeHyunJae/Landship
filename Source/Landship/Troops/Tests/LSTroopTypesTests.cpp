#if WITH_DEV_AUTOMATION_TESTS

#include "Troops/LSTroopUnit.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopTypesTest, "Landship.Troops.TypesAndCombat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopTypesTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TArray<ALSTroopUnit*> Units;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ALSTroopUnit* Unit = World->SpawnActor<ALSTroopUnit>(
		    ALSTroopUnit::StaticClass(), FVector(0, Index * 300 - 300, 200), FRotator::ZeroRotator, SpawnParameters);
		if (!TestNotNull(TEXT("병력 생성"), Unit))
		{
			return false;
		}
		TestTrue(TEXT("초기 병종 지정"), Unit->SetInitialTroopType(static_cast<ELSTroopType>(Index)));
		Units.Add(Unit);
	}
	ALSTroopUnit* Enemy = World->SpawnActor<ALSTroopUnit>(ALSTroopUnit::StaticClass(), FVector(500, 0, 200),
	                                                      FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("동일 클래스 적 생성"), Enemy))
	{
		return false;
	}
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	TArray<FLSUnitStatRow> Stats;
	for (ALSTroopUnit* Unit : Units)
	{
		TestNotNull(TEXT("기본 아바타 메시 준비"), Unit->GetMesh()->GetSkeletalMeshAsset());
		TestNotNull(TEXT("아바타 랙돌 Physics Asset 준비"), Unit->GetMesh()->GetPhysicsAsset());
		TestTrue(TEXT("병종 표식은 캡슐 머리 위에 배치"),
		         Unit->GetTroopMarker()->GetRelativeLocation().Z > Unit->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		TestTrue(TEXT("표식은 이동 충돌에 관여하지 않음"),
		         Unit->GetTroopMarker()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		FLSUnitStatRow Row;
		TestTrue(TEXT("병종 행 초기화"), Unit->TryGetStats(Row));
		Stats.Add(Row);
		TestEqual(TEXT("이동 컴포넌트 수치 연결"), Unit->GetCharacterMovement()->MaxWalkSpeed, Row.MovementSpeed);
		TestEqual(TEXT("기본 공격 사거리 연결"), Unit->BasicAttack->GetAttackSettings().Range, Row.AttackRange);
		FLSHealthSnapshot Snapshot;
		TestTrue(TEXT("공통 체력 준비"), Unit->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot));
		TestEqual(TEXT("공통 최대 체력 연결"), Snapshot.MaxHealth, Row.Health);
	}
	TestEqual(TEXT("궁병과 창병 이동 속도 동일"), Stats[1].MovementSpeed, Stats[0].MovementSpeed);
	TestEqual(TEXT("기마병 이동 속도 두 배"), Stats[2].MovementSpeed, Stats[0].MovementSpeed * 2);
	TestEqual(TEXT("창병과 기마병 근접 사거리 동일"), Stats[2].AttackRange, Stats[0].AttackRange);
	TestTrue(TEXT("궁병은 더 긴 공격 사거리"), Stats[1].AttackRange > Stats[0].AttackRange);
	TestTrue(TEXT("서로 다른 세 도형"),
	         Units[0]->GetTroopMarker()->GetStaticMesh() != Units[1]->GetTroopMarker()->GetStaticMesh() &&
	             Units[1]->GetTroopMarker()->GetStaticMesh() != Units[2]->GetTroopMarker()->GetStaticMesh() &&
	             Units[0]->GetTroopMarker()->GetStaticMesh() != Units[2]->GetTroopMarker()->GetStaticMesh());
	TestTrue(TEXT("세 병종이 동일한 아바타를 사용"),
	         Units[0]->GetMesh()->GetSkeletalMeshAsset() == Units[1]->GetMesh()->GetSkeletalMeshAsset() &&
	             Units[1]->GetMesh()->GetSkeletalMeshAsset() == Units[2]->GetMesh()->GetSkeletalMeshAsset());
	TestTrue(TEXT("같은 클래스의 병종을 별도로 선택 구분"),
	         Units[0]->Selection->GetSelectionType() != Units[1]->Selection->GetSelectionType() &&
	             Units[1]->Selection->GetSelectionType() != Units[2]->Selection->GetSelectionType());
	TestTrue(TEXT("동일 클래스의 반대 진영 표적 허용"), ULSCombatTargetLibrary::CanAutoAttackTarget(Units[0], Enemy));
	TestFalse(TEXT("같은 진영 병종 오폭 제외"), ULSCombatTargetLibrary::CanAutoAttackTarget(Units[0], Units[1]));
	TestFalse(TEXT("전투 중 병종 변경 거부"), Units[0]->SetInitialTroopType(ELSTroopType::Cavalry));
	TestFalse(TEXT("잘못된 병종 거부"), Units[0]->SetInitialTroopType(static_cast<ELSTroopType>(255)));
	TestEqual(TEXT("거부 시 기존 병종 보존"), Units[0]->GetTroopType(), ELSTroopType::Spearman);
	ULSHealthComponent* EnemyHealth = Enemy->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot Before;
	EnemyHealth->TryGetHealth(Before);
	TestTrue(TEXT("사거리 밖 근접 대상 지정 허용"), Units[0]->BasicAttack->StartAttack(Enemy));
	FLSHealthSnapshot AfterMelee;
	EnemyHealth->TryGetHealth(AfterMelee);
	TestEqual(TEXT("창병은 사거리 밖에서 피해 없음"), AfterMelee.CurrentHealth, Before.CurrentHealth);
	Units[0]->BasicAttack->StopAttack();
	TestTrue(TEXT("궁병 원거리 공격"), Units[1]->BasicAttack->StartAttack(Enemy));
	FLSHealthSnapshot AfterLaunch;
	EnemyHealth->TryGetHealth(AfterLaunch);
	TestEqual(TEXT("궁병은 발사 직후 피해 없음"), AfterLaunch.CurrentHealth, Before.CurrentHealth);
	Units[1]->BasicAttack->StopAttack();
	for (ALSTroopUnit* Unit : Units)
	{
		Unit->Orders->SetComponentTickEnabled(false);
		Unit->GetCharacterMovement()->DisableMovement();
	}
	Enemy->Orders->SetComponentTickEnabled(false);
	Enemy->GetCharacterMovement()->DisableMovement();
	for (int32 Index = 0; Index < 60; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	FLSHealthSnapshot AfterRanged;
	EnemyHealth->TryGetHealth(AfterRanged);
	TestEqual(TEXT("화살 명중 후 실제 공통 체력 감소"), AfterRanged.CurrentHealth,
	          Before.CurrentHealth - Stats[1].AttackPower);
	TestTrue(TEXT("궁병만 투사체 모드 사용"),
	         Units[1]->BasicAttack->GetProjectileSettings().ProjectileClass != nullptr &&
	             Units[0]->BasicAttack->GetProjectileSettings().ProjectileClass == nullptr &&
	             Units[2]->BasicAttack->GetProjectileSettings().ProjectileClass == nullptr);
	EnemyHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10000, Units[1], Units[1]));
	TestFalse(TEXT("사망한 병종 표식 숨김"), Enemy->GetTroopMarker()->IsVisible());
	TestTrue(TEXT("아바타 공통 사망 랙돌 적용"), Enemy->GetMesh()->IsSimulatingPhysics());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
