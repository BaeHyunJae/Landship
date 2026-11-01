#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

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
	ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>(FVector(400, 0, 142), FRotator::ZeroRotator, Spawn);
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
	Castle->BasicAttack->StopAttack();
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

#endif
