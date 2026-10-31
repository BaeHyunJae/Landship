#include "Vision/LSVisionGeometry.h"
#include <cmath>

namespace
{
bool IsFinite(FVector2D Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
}

bool HasPositiveSize(const FBox2D& Bounds)
{
	const FVector2D Size = Bounds.GetSize();
	return Bounds.bIsValid && IsFinite(Bounds.Min) && IsFinite(Bounds.Max) && IsFinite(Size) &&
		   Size.X > 0.0 && Size.Y > 0.0;
}
} // namespace

bool LSVisionGeometry::IsInsideSight(FVector2D Point, FVector2D Center, double Radius)
{
	if (!IsFinite(Point) || !IsFinite(Center) || !FMath::IsFinite(Radius) || Radius < 0.0)
	{
		return false;
	}
	const FVector2D Delta = Point - Center;
	if (!IsFinite(Delta))
	{
		return false;
	}
	// hypot은 큰 좌표의 제곱 넘침과 정규화 나눗셈에 따른 원 둘레 오차를 피한다.
	return std::hypot(Delta.X, Delta.Y) <= Radius;
}

bool LSVisionGeometry::WorldToUV(FVector2D Point, const FBox2D& Bounds, FVector2D& OutUV)
{
	if (!IsFinite(Point) || !HasPositiveSize(Bounds))
	{
		return false;
	}
	const FVector2D Size = Bounds.GetSize();
	const FVector2D UV((Point.Y - Bounds.Min.Y) / Size.Y, 1.0 - (Point.X - Bounds.Min.X) / Size.X);
	if (!IsFinite(UV))
	{
		return false;
	}
	OutUV = UV;
	return true;
}

bool LSVisionGeometry::UVToWorld(FVector2D UV, const FBox2D& Bounds, FVector2D& OutPoint)
{
	if (!IsFinite(UV) || !HasPositiveSize(Bounds))
	{
		return false;
	}
	const FVector2D Size = Bounds.GetSize();
	const FVector2D Point(Bounds.Min.X + (1.0 - UV.Y) * Size.X, Bounds.Min.Y + UV.X * Size.Y);
	if (!IsFinite(Point))
	{
		return false;
	}
	OutPoint = Point;
	return true;
}

FBox2D LSVisionGeometry::FitMapRect(const FBox2D& Available, const FBox2D& WorldBounds)
{
	if (!HasPositiveSize(Available) || !HasPositiveSize(WorldBounds))
	{
		return FBox2D(ForceInit);
	}
	const FVector2D WorldSize = WorldBounds.GetSize();
	const FVector2D MapSize(WorldSize.Y, WorldSize.X);
	const FVector2D AvailableSize = Available.GetSize();
	const double Scale = FMath::Min(AvailableSize.X / MapSize.X, AvailableSize.Y / MapSize.Y);
	const FVector2D FittedSize = MapSize * Scale;
	const FVector2D Min = Available.Min + (AvailableSize - FittedSize) * 0.5;
	if (!IsFinite(FittedSize) || !IsFinite(Min) || FittedSize.X <= 0.0 || FittedSize.Y <= 0.0)
	{
		return FBox2D(ForceInit);
	}
	return FBox2D(Min, Min + FittedSize);
}
