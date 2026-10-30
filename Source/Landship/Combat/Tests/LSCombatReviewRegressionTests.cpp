#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Tests/LSHealthTestObserver.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Troops/LSUnit.h"
#include "Castle/LSCastle.h"
#include "Enemies/LSOutpost.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
FLSDamageRequest ReviewDamage(float Amount)
{
	FLSDamageRequest Request;
	Request.DamageAmount = Amount;
	return Request;
}

ULSHealthTestObserver* ReviewObserver(UWorld* World, ULSHealthComponent* Health)
{
	// 소유자가 알림 도중 제거돼도 같은 호출 스택의 결과를 계속 관찰한다.
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(World);
	Health->OnDamagedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDamage);
	Health->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	return Observer;
}

ALSUnit* SpawnReviewUnit(UWorld* World, UClass* Class, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ALSUnit>(Class, Location, FRotator::ZeroRotator, Spawn);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatFailedAISpawnTest, "Landship.Combat.Review.FailedAISpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatFailedAISpawnTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	if (!TestTrue(TEXT("게임 시작"), World.BeginPlayInTestWorld())) return false;
	const FString Prefix = TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed:");
	AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 1);
	FEnsureScope EnsureScope([&](const FEnsureHandlerArgs& Args)
	{
		if (!FString(Args.Message).StartsWith(Prefix)) return false;
		AddError(Args.Message);
		return true;
	});
	ALSUnit* Unit = Cast<ALSUnit>(UAIBlueprintHelperLibrary::SpawnAIFromClass(World.GetTestWorld(),
		ALSUnit::StaticClass(), nullptr, FVector(0, 0, 500), FRotator::ZeroRotator, true));
	if (!TestNotNull(TEXT("설정 실패 유닛 AI 생성"), Unit)) return false;
	TestEqual(TEXT("실제 수치 실패 진단 한 번"), EnsureScope.GetCount(), 1);
	TestNotNull(TEXT("실패 뒤에도 컨트롤러 생성 경로 실행"), Unit->GetController());
	TestEqual(TEXT("실패 후 AI 소유가 이동을 되살리지 않음"), Unit->GetCharacterMovement()->MovementMode, MOVE_None);
	TestEqual(TEXT("실패 캡슐 충돌 해제 유지"), Unit->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	ULSHealthComponent* Health = Unit->FindComponentByClass<ULSHealthComponent>();
	if (!TestNotNull(TEXT("실패 대상 체력"), Health)) return false;
	TestEqual(TEXT("체력 실패 유지"), Health->GetInitializationState(), ELSHealthInitializationState::Failed);
	const FVector Position = Unit->GetActorLocation();
	TestTrue(TEXT("실패 후 게임 시간 진행"), World.TickTestWorld(0.35f));
	TestTrue(TEXT("실패 대상이 중력으로 떨어지지 않음"), Unit->GetActorLocation().Equals(Position, 0.1f));
	TestFalse(TEXT("실패를 사망 제거로 치환하지 않음"), Unit->IsActorBeingDestroyed());
	TestEqual(TEXT("실패 대상 수명 타이머 없음"), Unit->GetLifeSpan(), 0.0f);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatDeadRepossessTest, "Landship.Combat.Review.DeadRepossess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatDeadRepossessTest::RunTest(const FString& Parameters)
{
	UClass* PlainClass = LoadClass<ALSUnit>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C"));
	UClass* HumanoidClass = LoadClass<ALSUnit>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestHumanoid.BP_TestHumanoid_C"));
	if (!TestNotNull(TEXT("일반 유닛 Blueprint"), PlainClass) || !TestNotNull(TEXT("인간형 유닛 Blueprint"), HumanoidClass)) return false;
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("게임 월드 생성"), World.CreateTestWorld(EWorldType::Game))) return false;
	ALSUnit* Plain = SpawnReviewUnit(World.GetTestWorld(), PlainClass, FVector(0, 0, 500));
	ALSUnit* Humanoid = SpawnReviewUnit(World.GetTestWorld(), HumanoidClass, FVector(1000, 0, 500));
	if (!TestNotNull(TEXT("일반 유닛 생성"), Plain) || !TestNotNull(TEXT("인간형 유닛 생성"), Humanoid)) return false;
	for (ALSUnit* Unit : {Plain, Humanoid}) Unit->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
	if (!TestTrue(TEXT("게임 시작"), World.BeginPlayInTestWorld())) return false;
	for (ALSUnit* Unit : {Plain, Humanoid})
	{
		ULSHealthComponent* Health = Unit->FindComponentByClass<ULSHealthComponent>();
		TestEqual(TEXT("정상 대상 준비"), Health->GetInitializationState(), ELSHealthInitializationState::Ready);
		if (!Unit->GetController()) Unit->SpawnDefaultController();
		AController* Controller = Unit->GetController();
		if (!TestNotNull(TEXT("사망 전 컨트롤러"), Controller)) return false;
		ULSHealthTestObserver* Observer = ReviewObserver(World.GetTestWorld(), Health);
		Health->ApplyDamage(ReviewDamage(100000.0f));
		Controller->UnPossess();
		Unit->SpawnDefaultController();
		TestNotNull(TEXT("사망 후 새 컨트롤러 소유"), Unit->GetController());
		TestEqual(TEXT("재소유가 사망 유닛 이동을 되살리지 않음"), Unit->GetCharacterMovement()->MovementMode, MOVE_None);
		TestEqual(TEXT("재소유 뒤 캡슐 비활성 유지"), Unit->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestEqual(TEXT("사망 알림은 재소유해도 한 번"), Observer->DeathResults.Num(), 1);
		// 사망 판정이 입력 검사보다 앞서므로 NaN 재피격도 정상 사망 거부다.
		TestEqual(TEXT("사망 대상 NaN은 입력 ensure 없이 사망 거부"),
			Health->ApplyDamage(ReviewDamage(std::numeric_limits<float>::quiet_NaN())).Status, ELSApplyDamageStatus::RejectedDead);
		TestEqual(TEXT("NaN 재피격도 추가 사망 알림 없음"), Observer->DeathResults.Num(), 1);
	}
	const FVector PlainPosition = Plain->GetActorLocation();
	TestTrue(TEXT("재소유 후 게임 시간 진행"), World.TickTestWorld(0.35f));
	TestTrue(TEXT("일반 사망 유닛 위치 유지"), Plain->GetActorLocation().Equals(PlainPosition, 0.1f));
	TestTrue(TEXT("인간형 재소유 뒤 랙돌 시뮬레이션 유지"), Humanoid->GetMesh()->IsSimulatingPhysics());
	TestEqual(TEXT("인간형 랙돌 지면 충돌 유지"), Humanoid->GetMesh()->GetCollisionResponseToChannel(LSCollisionChannels::Ground), ECR_Block);
	ALSUnit* Healthy = Cast<ALSUnit>(UAIBlueprintHelperLibrary::SpawnAIFromClass(World.GetTestWorld(),
		PlainClass, nullptr, FVector(2000, 0, 500), FRotator::ZeroRotator, true));
	if (TestNotNull(TEXT("정상 Blueprint AI 생성"), Healthy))
	{
		TestNotNull(TEXT("정상 AI 컨트롤러"), Healthy->GetController());
		TestTrue(TEXT("정상 AI 이동 모드는 비활성화하지 않음"), Healthy->GetCharacterMovement()->MovementMode != MOVE_None);
		TestEqual(TEXT("정상 AI 체력 준비 유지"), Healthy->FindComponentByClass<ULSHealthComponent>()->GetInitializationState(), ELSHealthInitializationState::Ready);
	}
	World.ForwardErrorMessages(this);
	return true;
}


#endif
