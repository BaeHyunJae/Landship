#pragma once

#include "CoreMinimal.h"

/** 월드 XY를 화면 위 +X, 오른쪽 +Y인 지도 좌표로 바꾼다. UObject 없이 검사할 수 있다. */
namespace LSVisionGeometry
{
LANDSHIP_API bool IsInsideSight(FVector2D Point, FVector2D Center, double Radius);

/** 클리핑은 호출자가 맡도록 범위 밖 UV도 반환한다. 잘못된 입력이면 false이며 출력을 보존한다. */
LANDSHIP_API bool WorldToUV(FVector2D Point, const FBox2D& Bounds, FVector2D& OutUV);
LANDSHIP_API bool UVToWorld(FVector2D UV, const FBox2D& Bounds, FVector2D& OutPoint);

/** 월드 Y를 너비, X를 높이로 삼아 가운데 맞춘다. 잘못된 입력이면 유효하지 않은 상자를 반환한다. */
LANDSHIP_API FBox2D FitMapRect(const FBox2D& Available, const FBox2D& WorldBounds);
} // namespace LSVisionGeometry
