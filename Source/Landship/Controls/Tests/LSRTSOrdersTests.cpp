#if WITH_DEV_AUTOMATION_TESTS

#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Troops/LSUnit.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

namespace
{
	ULSSelectableComponent* AddSelection(AActor* Actor, bool bControl = true, ELSSelectionKind Kind = ELSSelectionKind::Unit)
	{
		ULSSelectableComponent* Selection = NewObject<ULSSelectableComponent>(Actor);
		Actor->AddInstanceComponent(Selection);
		Selection->Kind = Kind;
		Selection->SetPlayerControlled(bControl);
		Selection->RegisterComponent();
		return Selection;
	}

	void AdvanceRTS(UWorld* World, float Seconds)
	{
		while (Seconds > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Seconds, 0.01f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			Seconds -= Step;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRTSSelectionTest, "Landship.Controls.SelectionAndGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRTSSelectionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	ALSBasicAttackTestActor* A = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* B = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Building = World->SpawnActor<ALSBasicAttackTestActor>();
	AddSelection(A); AddSelection(B); AddSelection(Enemy, false); AddSelection(Building, true, ELSSelectionKind::Building);
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	Controller->ApplySelection({ A, B, Enemy, Building }, false, true);
	TestEqual(TEXT("혼합 드래그에서 아군 병력 우선"), Controller->GetSelectedActors().Num(), 2);
	Controller->ApplySelection({ A }, true);
	TestEqual(TEXT("Shift 기존 대상 제외"), Controller->GetSelectedActors().Num(), 1);
	Controller->ApplySelection({ A }, true);
	TestEqual(TEXT("Shift 대상 추가"), Controller->GetSelectedActors().Num(), 2);
	Controller->UseControlGroup(1, true);
	Controller->ApplySelection({ Enemy });
	TestEqual(TEXT("적 단독 정보 선택"), Controller->GetSelectedActors().Num(), 1);
	TestFalse(TEXT("적 명령 거부"), Controller->IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false));
	Controller->ApplySelection({ A }, true);
	TestEqual(TEXT("적과 아군을 혼합 선택하지 않음"), Controller->GetSelectedActors().Num(), 1);
	Controller->UseControlGroup(1, false);
	TestEqual(TEXT("부대 복원"), Controller->GetSelectedActors().Num(), 2);
	A->Destroy();
	Controller->UseControlGroup(1, false);
	TestEqual(TEXT("부대에서 파괴된 대상 제외"), Controller->GetSelectedActors().Num(), 1);
	B->Health = 0;
	Controller->UseControlGroup(1, false);
	TestTrue(TEXT("부대에서 사망 대상 제외"), Controller->GetSelectedActors().IsEmpty());
	Controller->SetPendingInput(ELSCommandInput::Attack);
	Controller->CancelInput();
	TestTrue(TEXT("명령 입력 취소"), Controller->GetPendingInput() == ELSCommandInput::None);
	const TArray<FVector> Points = ULSOrderComponent::MakeFormation(FVector(500, 500, 0), 7, 150);
	TestEqual(TEXT("목적지 수"), Points.Num(), 7);
	FVector Mean = FVector::ZeroVector;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		Mean += Points[I];
		for (int32 J = I + 1; J < Points.Num(); ++J) { TestTrue(TEXT("목적지 분리"), FVector::Dist(Points[I], Points[J]) >= 149); }
	}
	TestTrue(TEXT("대형 중심 보존"), (Mean / Points.Num()).Equals(FVector(500, 500, 0), 0.01));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRTSCombatOrdersTest, "Landship.Controls.CombatOrders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRTSCombatOrdersTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Other = World->SpawnActor<ALSBasicAttackTestActor>();
	Enemy->Team = Other->Team = 1;
	Enemy->SetActorLocation(FVector(150, 0, 0));
	Other->SetActorLocation(FVector(180, 0, 0));
	AddSelection(Source);
	ULSOrderComponent* Orders = NewObject<ULSOrderComponent>(Source);
	Source->AddInstanceComponent(Orders); Orders->RegisterComponent();
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	FLSBasicAttackSettings Settings; Settings.Damage = 10; Settings.Range = 200; Settings.Interval = 0.2f;
	Source->Attack->ConfigureAttack(Settings);
	FLSOrder Hold; Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("위치 사수 명령"), Orders->SubmitOrder(Hold));
	AdvanceRTS(World, 0.08f);
	TestEqual(TEXT("사거리 안 적 공격"), Enemy->Health, 90.0f);
	Other->SetActorLocation(FVector(100, 0, 0));
	AdvanceRTS(World, 0.25f);
	TestEqual(TEXT("기존 공격 대상 유지"), Enemy->Health, 80.0f);
	TestEqual(TEXT("더 가까운 적으로 임의 변경 없음"), Other->Health, 100.0f);
	FLSOrder Attack; Attack.Kind = ELSOrderKind::AttackTarget; Attack.Target = Enemy;
	TestTrue(TEXT("지정 공격"), Orders->SubmitOrder(Attack));
	Attack.Target = Other;
	TestTrue(TEXT("공격 명령 예약"), Orders->SubmitOrder(Attack, true));
	TestEqual(TEXT("예약 수"), Orders->GetQueuedCount(), 1);
	Enemy->Health = 0;
	AdvanceRTS(World, 0.3f);
	TestEqual(TEXT("대상 사망 뒤 예약 실행"), Orders->GetQueuedCount(), 0);
	TestTrue(TEXT("예약 대상 공격"), Other->Health < 100);
	FLSOrder Invalid; Invalid.Kind = ELSOrderKind::Move; Invalid.Destination = FVector(1.e8);
	TestFalse(TEXT("이동 불가능한 대상의 이동 거부"), Orders->SubmitOrder(Invalid));
	TestTrue(TEXT("거부 시 기존 명령 보존"), Orders->GetOrderKind() == ELSOrderKind::AttackTarget);
	for (int32 I = 0; I < 64; ++I) { TestTrue(TEXT("예약 추가"), Orders->SubmitOrder(Attack, true)); }
	TestFalse(TEXT("예약 상한"), Orders->SubmitOrder(Attack, true));
	TestTrue(TEXT("정지"), Orders->SubmitOrder(FLSOrder()));
	TestEqual(TEXT("정지 시 예약 제거"), Orders->GetQueuedCount(), 0);
	Source->Health = 0;
	AdvanceRTS(World, 0.3f);
	TestNull(TEXT("공격자 사망 시 교전 해제"), Orders->GetEngagedTarget());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRTSClickCollisionTest, "Landship.Controls.UnitClickCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRTSClickCollisionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game))) { return false; }
	UClass* UnitClass = LoadClass<ALSUnit>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C"));
	if (!TestNotNull(TEXT("실제 유닛 BP"), UnitClass)) { return false; }
	ALSUnit* Unit = TestWorld.GetTestWorld()->SpawnActor<ALSUnit>(UnitClass);
	if (!TestNotNull(TEXT("유닛 생성"), Unit) || !TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld())) { return false; }
	FHitResult Hit;
	const FVector Center = Unit->GetActorLocation();
	TestTrue(TEXT("Visibility 클릭 광선 적중"), TestWorld.GetTestWorld()->LineTraceSingleByChannel(Hit,
		Center - FVector(0, 500, 0), Center + FVector(0, 500, 0), ECC_Visibility));
	TestEqual(TEXT("바닥 대신 유닛을 선택"), Hit.GetActor(), static_cast<AActor*>(Unit));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
