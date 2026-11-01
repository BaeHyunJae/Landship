#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/LSEnemyDecisionPolicy.h"
#include "Misc/AutomationTest.h"

using namespace LSEnemyDecisionPolicy;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyTargetScoreTest,
	"Landship.Enemies.Policy.TargetPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyTargetScoreTest::RunTest(const FString& Parameters)
{
	const FLSEnemyTargetScore Current{40.0f, 100.0, 20, true};
	TestTrue(TEXT("낮은 현재 체력이 현재 표적 유지보다 우선"),
		IsBetterTarget({39.0f, 10000.0, 30, false}, Current));
	TestFalse(TEXT("높은 체력은 가까워도 선택하지 않음"),
		IsBetterTarget({41.0f, 1.0, 1, false}, Current));
	TestFalse(TEXT("같은 체력이면 현재 표적이 거리와 식별값보다 우선"),
		IsBetterTarget({40.0f, 1.0, 1, false}, Current));
	TestTrue(TEXT("같은 체력의 현재 표적을 유지"),
		IsBetterTarget(Current, {40.0f, 1.0, 1, false}));
	TestTrue(TEXT("현재 표적이 없으면 가까운 후보 우선"),
		IsBetterTarget({40.0f, 4.0, 30, false}, {40.0f, 9.0, 1, false}));
	TestTrue(TEXT("체력과 거리가 같으면 안정된 식별값 순"),
		IsBetterTarget({40.0f, 4.0, 1, false}, {40.0f, 4.0, 2, false}));
	TestFalse(TEXT("동일 점수는 기존 선택을 바꾸지 않음"), IsBetterTarget(Current, Current));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyRouteSampleTest,
	"Landship.Enemies.Policy.RouteProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyRouteSampleTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Empty;
	TestFalse(TEXT("빈 경로는 조회 실패"), SampleRoute(Empty, FVector::ZeroVector).bValid);
	const TArray<FVector> Route{FVector(0, 0, 100), FVector(100, 0, 200), FVector(100, 100, 300)};
	const FLSEnemyRouteSample Corner = SampleRoute(Route, FVector(100, 50, 900));
	TestTrue(TEXT("모서리 경로 투영 성공"), Corner.bValid);
	TestEqual(TEXT("가장 가까운 두 번째 선분의 경로 진행량"), Corner.Progress, 150.0f);
	TestEqual(TEXT("높이 차이를 제외한 전체 수평 경로 길이"), Corner.Length, 200.0f);
	TestEqual(TEXT("높이 차이는 수평 이탈이 아님"), Corner.DistanceFromRoute, 0.0f);
	const FLSEnemyRouteSample Offset = SampleRoute(Route, FVector(110, 50, -100));
	TestEqual(TEXT("가장 가까운 선분까지 수평 거리"), Offset.DistanceFromRoute, 10.0f);
	TestEqual(TEXT("옆으로 이탈해도 경로 진행량 유지"), Offset.Progress, 150.0f);
	TestEqual(TEXT("경로 시작 전 투영 제한"), SampleRoute(Route, FVector(-10, 0, 0)).Progress, 0.0f);
	TestEqual(TEXT("경로 끝 뒤 투영 제한"), SampleRoute(Route, FVector(100, 120, 0)).Progress, 200.0f);
	const TArray<FVector> Duplicate{FVector::ZeroVector, FVector::ZeroVector, FVector(100, 0, 0)};
	TestEqual(TEXT("길이가 0인 선분 뒤의 투영"), SampleRoute(Duplicate, FVector(25, 0, 0)).Progress, 25.0f);
	const TArray<FVector> Stationary{FVector(20, 30, 0), FVector(20, 30, 0)};
	const FLSEnemyRouteSample StationarySample = SampleRoute(Stationary, FVector(23, 34, 200));
	TestTrue(TEXT("모든 선분 길이가 0이어도 유효"), StationarySample.bValid);
	TestEqual(TEXT("정지 경로 거리"), StationarySample.DistanceFromRoute, 5.0f);
	TestEqual(TEXT("정지 경로 길이"), StationarySample.Length, 0.0f);
	const TArray<FVector> Goal{FVector(20, 30, 100)};
	const FLSEnemyRouteSample AtGoal = SampleRoute(Goal, FVector(20, 30, 500));
	TestTrue(TEXT("목표 한 점 경로도 유효"), AtGoal.bValid);
	TestEqual(TEXT("한 점 목표 도착 진행량"), AtGoal.Progress, 0.0f);
	TestEqual(TEXT("한 점 목표 남은 경로 길이"), AtGoal.Length, 0.0f);
	TestEqual(TEXT("한 점 목표 거리"), AtGoal.DistanceFromRoute, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSEnemyRecoveryDistanceTest,
	"Landship.Enemies.Policy.RecoveryCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSEnemyRecoveryDistanceTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Route{FVector::ZeroVector, FVector(100, 0, 0)};
	float FurthestProgress = 0.0f;
	for (const FVector& Position : {FVector(20, 0, 0), FVector(10, 30, 0), FVector(20, -30, 0),
		FVector(10, 30, 0), FVector(20, 0, 0)})
	{
		FurthestProgress = FMath::Max(FurthestProgress, SampleRoute(Route, Position).Progress);
	}
	TestEqual(TEXT("왕복과 측면 회피 거리를 전진으로 누적하지 않음"), FurthestProgress, 20.0f);
	TestFalse(TEXT("왕복과 회피만으로 회복 완료하지 않음"), IsRecoveryComplete(FurthestProgress, 300, false));
	TestFalse(TEXT("거리 미달이고 목표에 도착하지 않으면 대기"), IsRecoveryComplete(100, 300, false));
	TestTrue(TEXT("실제 전진량 충족으로 완료"), IsRecoveryComplete(300, 300, false));
	TestTrue(TEXT("짧은 경로는 실제 전략 공격 위치 도달로 완료"), IsRecoveryComplete(100, 300, true));
	TestFalse(TEXT("경로 실패를 도착으로 세지 않음"), IsRecoveryComplete(0, 300, false));
	return true;
}

#endif
