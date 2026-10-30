#if WITH_DEV_AUTOMATION_TESTS

#include "Controls/LSRTSCameraPawn.h"
#include "Match/LSGameMode.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRTSCameraTest, "Landship.Controls.RTSCamera.BoundsAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRTSCameraTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper World;
	if (!TestTrue(TEXT("월드 생성"), World.CreateTestWorld(EWorldType::Game))) { return false; }
	ALSRTSCameraPawn* Camera = World.GetTestWorld()->SpawnActor<ALSRTSCameraPawn>();
	if (!TestNotNull(TEXT("카메라 생성"), Camera)) { return false; }
	TestTrue(TEXT("게임 모드 연결"), GetDefault<ALSGameMode>()->DefaultPawnClass == ALSRTSCameraPawn::StaticClass());
	Camera->FocusLocation(FVector::ZeroVector);
	Camera->Pan(FVector2D(0, 1), 0.1f);
	TestTrue(TEXT("화면 위 방향 이동"), Camera->GetActorLocation().Equals(FVector(180, 0, 0), 0.01));
	Camera->FocusLocation(FVector::ZeroVector);
	Camera->Pan(FVector2D(1, 1), 0.1f);
	TestTrue(TEXT("대각선 속도 정규화"), FMath::IsNearlyEqual(Camera->GetActorLocation().Size(), 180.0, 0.01));
	Camera->FocusLocation(FVector(1.e6, -1.e6, 200));
	TestTrue(TEXT("초점 경계와 높이 제한"), Camera->GetActorLocation().Equals(FVector(2400, -2000, 0)));
	Camera->Zoom(1000);
	TestEqual(TEXT("최소 줌"), Camera->GetZoomDistance(), 700.0f);
	Camera->Zoom(-1000);
	TestEqual(TEXT("최대 줌"), Camera->GetZoomDistance(), 4500.0f);
	Camera->Zoom(std::numeric_limits<float>::quiet_NaN());
	TestEqual(TEXT("잘못된 줌 무시"), Camera->GetZoomDistance(), 4500.0f);
	Camera->FocusLocation(FVector::ZeroVector);
	Camera->Drag(FVector2D(100, 0));
	TestTrue(TEXT("드래그는 지면을 끄는 방향"), Camera->GetActorLocation().Y < 0);
	const FVector Previous = Camera->GetActorLocation();
	Camera->Pan(FVector2D(1, 1), -1);
	TestEqual(TEXT("음수 시간 무시"), Camera->GetActorLocation(), Previous);
	World.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRTSEdgeTest, "Landship.Controls.RTSCamera.EdgeInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRTSEdgeTest::RunTest(const FString& Parameters)
{
	const FVector2D Size(1920, 1080);
	TestEqual(TEXT("중앙"), ALSRTSCameraPawn::CalculateEdgeDirection(Size / 2, Size, 12), FVector2D::ZeroVector);
	TestEqual(TEXT("좌상단"), ALSRTSCameraPawn::CalculateEdgeDirection(FVector2D(1, 1), Size, 12), FVector2D(-1, 1));
	TestEqual(TEXT("우하단"), ALSRTSCameraPawn::CalculateEdgeDirection(FVector2D(1919, 1079), Size, 12), FVector2D(1, -1));
	TestEqual(TEXT("창 밖"), ALSRTSCameraPawn::CalculateEdgeDirection(FVector2D(-1, 500), Size, 12), FVector2D::ZeroVector);
	TestEqual(TEXT("최소화"), ALSRTSCameraPawn::CalculateEdgeDirection(FVector2D::ZeroVector, FVector2D::ZeroVector, 12), FVector2D::ZeroVector);
	TestEqual(TEXT("기능 끄기"), ALSRTSCameraPawn::CalculateEdgeDirection(FVector2D(1, 1), Size, 0), FVector2D::ZeroVector);
	return true;
}

#endif
