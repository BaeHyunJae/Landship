#if WITH_DEV_AUTOMATION_TESTS

#include "Vision/LSVisionGeometry.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionGeometryTest, "Landship.Vision.Geometry",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionGeometryTest::RunTest(const FString& Parameters)
{
	using namespace LSVisionGeometry;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestTrue(TEXT("원 둘레 포함"), IsInsideSight(FVector2D(3, 4), FVector2D::ZeroVector, 5));
	TestFalse(TEXT("원 밖 제외"), IsInsideSight(FVector2D(3, 4.001), FVector2D::ZeroVector, 5));
	TestTrue(TEXT("반경 0 중심 포함"), IsInsideSight(FVector2D(7, 9), FVector2D(7, 9), 0));
	TestFalse(TEXT("반경 0 다른 점 제외"), IsInsideSight(FVector2D(7, 9.001), FVector2D(7, 9), 0));
	TestFalse(TEXT("음수 반경 제외"), IsInsideSight(FVector2D::ZeroVector, FVector2D::ZeroVector, -1));
	TestFalse(TEXT("유효하지 않은 반경 제외"),
			  IsInsideSight(FVector2D::ZeroVector, FVector2D::ZeroVector, NaN));
	TestFalse(TEXT("유효하지 않은 좌표 제외"), IsInsideSight(FVector2D(NaN, 0), FVector2D::ZeroVector, 10));
	const FVector HighPoint(3, 4, 9000);
	TestTrue(TEXT("높이를 제외한 XY 시야"), IsInsideSight(FVector2D(HighPoint), FVector2D::ZeroVector, 5));
	TestTrue(TEXT("큰 유한 반경"), IsInsideSight(FVector2D(1.e200, 0), FVector2D::ZeroVector, 1.e200));

	const FBox2D Bounds(FVector2D(100, -300), FVector2D(900, 100));
	FVector2D UV;
	TestTrue(TEXT("좌상단 변환"), WorldToUV(FVector2D(900, -300), Bounds, UV));
	TestEqual(TEXT("+X는 위"), UV, FVector2D(0, 0));
	TestTrue(TEXT("우하단 변환"), WorldToUV(FVector2D(100, 100), Bounds, UV));
	TestEqual(TEXT("+Y는 오른쪽"), UV, FVector2D(1, 1));
	const FVector2D Point(700, -220);
	FVector2D RoundTrip;
	TestTrue(TEXT("비정사각형 이동 경계 변환"), WorldToUV(Point, Bounds, UV));
	TestTrue(TEXT("역변환"), UVToWorld(UV, Bounds, RoundTrip));
	TestTrue(TEXT("왕복 좌표 보존"), RoundTrip.Equals(Point));
	TestTrue(TEXT("경계 밖 UV 허용"), WorldToUV(FVector2D(1300, -500), Bounds, UV));
	TestTrue(TEXT("UV를 제한하지 않음"), UV.X < 0 && UV.Y < 0);
	UV = FVector2D(12, 34);
	TestFalse(TEXT("크기 0 경계 거절"),
			  WorldToUV(Point, FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector), UV));
	TestEqual(TEXT("실패 시 출력 보존"), UV, FVector2D(12, 34));
	TestFalse(TEXT("NaN UV 거절"), UVToWorld(FVector2D(NaN, 0), Bounds, RoundTrip));

	const FBox2D Rect = FitMapRect(FBox2D(FVector2D(10, 20), FVector2D(210, 220)), Bounds);
	TestTrue(TEXT("지도 사각형 유효"), Rect.bIsValid != 0);
	TestEqual(TEXT("세로 지도 좌우 여백"), Rect.Min, FVector2D(60, 20));
	TestEqual(TEXT("세로 지도 종횡비"), Rect.Max, FVector2D(160, 220));
	const FBox2D WideRect = FitMapRect(FBox2D(FVector2D(10, 20), FVector2D(210, 220)),
									   FBox2D(FVector2D::ZeroVector, FVector2D(100, 400)));
	TestEqual(TEXT("가로 지도 상하 여백"), WideRect.Min, FVector2D(10, 95));
	TestEqual(TEXT("가로 지도 종횡비"), WideRect.Max, FVector2D(210, 145));
	TestFalse(TEXT("유효하지 않은 영역 거절"), FitMapRect(FBox2D(ForceInit), Bounds).bIsValid != 0);
	return true;
}

#endif
