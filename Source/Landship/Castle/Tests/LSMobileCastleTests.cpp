#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMobileCastleTest, "Landship.Castle.MobileUnit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMobileCastleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>();
	ALSMobileCastle* Enemy = World->SpawnActor<ALSMobileCastle>(FVector(300, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("아군 성 생성"), Castle) || !TestNotNull(TEXT("적 성 생성"), Enemy))
	{
		return false;
	}
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	FLSUnitStatRow Stats;
	TestTrue(TEXT("공용 병력 수치 경로 사용"), Castle->TryGetStats(Stats));
	TestEqual(TEXT("성 초기 체력"), Stats.Health, 1000.0f);
	TestEqual(TEXT("성 이동 속도 연결"), Castle->GetCharacterMovement()->MaxWalkSpeed, Stats.MovementSpeed);
	TestEqual(TEXT("대형 충돌 반경"), Castle->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), 142.0f);
	TestEqual(TEXT("내비게이션과 충돌 반경 일치"), Castle->GetCharacterMovement()->NavAgentProps.AgentRadius, 142.0f);
	TestTrue(TEXT("성 선택 분류 유지"), Castle->Selection->Kind == ELSSelectionKind::Castle);
	TestTrue(TEXT("아군 성 조작 가능"), Castle->Selection->CanPlayerControl());
	TestFalse(TEXT("적 성 조작 차단"), Enemy->Selection->CanPlayerControl());
	FLSOrder Attack;
	Attack.Kind = ELSOrderKind::AttackTarget;
	Attack.Target = Enemy;
	TestFalse(TEXT("성의 기본 지정 공격 제거"), Castle->Orders->SubmitOrder(Attack));
	TestFalse(TEXT("성의 기본 자동 공격 제거"), Castle->BasicAttack->IsConfigured());
	TestTrue(TEXT("수동 대포 시전"), Castle->Weapons->CastWeapon(ELSCastleWeapon::Cannon, Enemy->GetActorLocation()) ==
	                                     ELSWeaponCastResult::Ready);
	FLSHealthSnapshot Snapshot;
	ULSHealthComponent* EnemyHealth = Enemy->FindComponentByClass<ULSHealthComponent>();
	EnemyHealth->TryGetHealth(Snapshot);
	TestEqual(TEXT("수동 대포 피해"), Snapshot.CurrentHealth,
	          1000.0f - Castle->Weapons->FindSettings(ELSCastleWeapon::Cannon)->Damage);
	EnemyHealth->ApplyDamage(ULSHealthComponent::MakeDamageRequest(10000, Castle, Castle));
	EnemyHealth->TryGetHealth(Snapshot);
	TestTrue(TEXT("공통 사망 처리와 성 보존"), Snapshot.bIsDead && IsValid(Enemy) && !EnemyHealth->bDestroyOnDeath);
	TestTrue(TEXT("사망 시 이동 차단"), Enemy->GetCharacterMovement()->MovementMode == MOVE_None);
	TestFalse(TEXT("사망 시 공격 차단"), Enemy->Orders->CanAttack());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
