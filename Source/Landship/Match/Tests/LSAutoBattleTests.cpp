#if WITH_DEV_AUTOMATION_TESTS

#include "Match/LSAutoBattleScenario.h"
#include "Controls/LSFreeCameraPawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAutoBattleScenarioTest, "Landship.AutoBattle.ScenarioValidation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAutoBattleScenarioTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	auto* Scenario = Play.GetTestWorld()->SpawnActor<ALSAutoBattleScenario>();
	if (!Scenario)
		return false;
	TestTrue(TEXT("기본 시나리오는 적 병력 없이 유효"), Scenario->EnemyFormation.IsEmpty() &&
	         Scenario->GetConfigurationError().IsEmpty());
	Scenario->UnitCosts.Remove(ELSTroopType::Archer);
	TestFalse(TEXT("누락 비용을 자동 대체하지 않음"), Scenario->GetConfigurationError().IsEmpty());
	Scenario->UnitCosts.Add(ELSTroopType::Archer, 60);
	Scenario->DeploymentMax.X = Scenario->BoundsMax.X + 1;
	TestFalse(TEXT("배치 구역의 전장 이탈 거부"), Scenario->GetConfigurationError().IsEmpty());
	Scenario->DeploymentMax.X = -300;
	Scenario->EnemyFormation.AddDefaulted_GetRef().Type = static_cast<ELSTroopType>(255);
	TestFalse(TEXT("잘못된 병종 거부"), Scenario->GetConfigurationError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSFreeCameraTest, "Landship.Controls.FreeCamera.MovementAndLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSFreeCameraTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Play.GetTestWorld();
	auto* Camera = World->SpawnActor<ALSFreeCameraPawn>();
	if (!Camera || !Play.BeginPlayInTestWorld())
		return false;
	Camera->SetActorLocation(FVector(0, 0, 1000));
	Camera->SetActorRotation(FRotator::ZeroRotator);
	const FVector Start = Camera->GetActorLocation();
	Camera->MoveCamera(FVector(1, 1, 1), 0.1f, false);
	TestTrue(TEXT("대각 이동의 합성 속도 제한"),
	         FMath::IsNearlyEqual(FVector::Dist(Start, Camera->GetActorLocation()), Camera->GetMoveSpeed() * 0.1, 0.1));
	const FVector BeforeInvalid = Camera->GetActorLocation();
	Camera->MoveCamera(FVector(1, 0, 0), -1, false);
	Camera->MoveCamera(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 1, false);
	TestEqual(TEXT("잘못된 이동 입력 보존"), Camera->GetActorLocation(), BeforeInvalid);
	Camera->SetActorRotation(FRotator::ZeroRotator);
	Camera->Look(FVector2D(10, 10));
	TestTrue(TEXT("에디터와 같이 오른쪽·위 마우스 입력이 오른쪽·위를 바라봄"),
	    Camera->GetActorRotation().Yaw > 0 && Camera->GetActorRotation().Pitch > 0);
	Camera->SetActorRotation(FRotator::ZeroRotator);
	const FVector BeforePan = Camera->GetActorLocation();
	Camera->Pan(FVector2D(20, 30));
	TestTrue(TEXT("가운데 드래그는 시점 회전 없이 화면 평면으로 이동"), Camera->GetActorRotation().IsNearlyZero() &&
	    Camera->GetActorLocation().Y > BeforePan.Y && Camera->GetActorLocation().Z > BeforePan.Z);
	const float SpeedBeforeDolly = Camera->GetMoveSpeed();
	const FVector BeforeDolly = Camera->GetActorLocation();
	Camera->Dolly(1);
	TestTrue(TEXT("일반 휠은 이동 속도를 바꾸지 않고 전진"), Camera->GetMoveSpeed() == SpeedBeforeDolly &&
	    Camera->GetActorLocation().X > BeforeDolly.X);
	Camera->Look(FVector2D(0, 100000));
	TestTrue(TEXT("1인칭 시점 뒤집힘 방지"), Camera->GetActorRotation().Pitch >= -85.1);
	Camera->ChangeSpeed(100);
	TestTrue(TEXT("관전 속도 상한"), FMath::IsFinite(Camera->GetMoveSpeed()) && Camera->GetMoveSpeed() <= 5000);
	Camera->MoveCamera(FVector::UpVector, 100, true);
	TestTrue(TEXT("전장 고도 상한"), Camera->GetActorLocation().Z <= 3500);
	Camera->ResetView();
	TestTrue(TEXT("기본 관전 위치 복귀"), Camera->GetActorLocation().Equals(FVector(-2000, -1600, 1800)));
	return true;
}
#endif
