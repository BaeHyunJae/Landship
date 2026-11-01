#include "Obstacles/LSObstacleGeometry.h"
#include "Obstacles/LSPathObstacle.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include <limits>

namespace
{
double PointBoxDistance(const UBoxComponent* Box, const FVector& Point, FVector* Closest = nullptr)
{
	const FVector Local = Box->GetComponentQuat().UnrotateVector(Point - Box->GetComponentLocation());
	const FVector Extent = Box->GetScaledBoxExtent();
	const FVector Clamped(FMath::Clamp(Local.X, -Extent.X, Extent.X), FMath::Clamp(Local.Y, -Extent.Y, Extent.Y),
						  FMath::Clamp(Local.Z, -Extent.Z, Extent.Z));
	if (Closest)
		*Closest = Box->GetComponentLocation() + Box->GetComponentQuat().RotateVector(Clamped);
	return FVector::Distance(Local, Clamped);
}
} // namespace

double LSObstacleGeometry::GetPlanarBodyDistance(const UPrimitiveComponent* Body, const FVector& Point)
{
	if (!IsValid(Body) || Point.ContainsNaN())
		return std::numeric_limits<double>::infinity();
	if (const USphereComponent* Sphere = Cast<USphereComponent>(Body))
		return FMath::Max(0.0, FVector::Dist2D(Point, Sphere->GetComponentLocation()) - Sphere->GetScaledSphereRadius());
	const UBoxComponent* Box = Cast<UBoxComponent>(Body);
	if (!Box)
		return std::numeric_limits<double>::infinity();
	TArray<FVector2D> Corners;
	const FVector Extent = Box->GetScaledBoxExtent();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector Local((Index & 1) ? Extent.X : -Extent.X, (Index & 2) ? Extent.Y : -Extent.Y,
			(Index & 4) ? Extent.Z : -Extent.Z);
		const FVector World = Box->GetComponentLocation() + Box->GetComponentQuat().RotateVector(Local);
		Corners.Add(FVector2D(World.X, World.Y));
	}
	Corners.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	};
	TArray<FVector2D> Hull;
	for (const FVector2D& Corner : Corners)
	{
		while (Hull.Num() >= 2 && Cross(Hull[Hull.Num() - 2], Hull.Last(), Corner) <= 0)
			Hull.Pop(EAllowShrinking::No);
		Hull.Add(Corner);
	}
	const int32 LowerCount = Hull.Num();
	for (int32 Index = Corners.Num() - 2; Index >= 0; --Index)
	{
		while (Hull.Num() > LowerCount && Cross(Hull[Hull.Num() - 2], Hull.Last(), Corners[Index]) <= 0)
			Hull.Pop(EAllowShrinking::No);
		Hull.Add(Corners[Index]);
	}
	if (Hull.Num() > 1)
		Hull.Pop(EAllowShrinking::No);
	const FVector2D Query(Point.X, Point.Y);
	double DistanceSquared = std::numeric_limits<double>::infinity();
	bool bInside = Hull.Num() >= 3;
	for (int32 Index = 0; Index < Hull.Num(); ++Index)
	{
		const FVector2D& A = Hull[Index];
		const FVector2D& B = Hull[(Index + 1) % Hull.Num()];
		const FVector2D Delta = B - A;
		const double Fraction = Delta.SizeSquared() > 0 ? FMath::Clamp(FVector2D::DotProduct(Query - A, Delta) / Delta.SizeSquared(), 0.0, 1.0) : 0;
		DistanceSquared = FMath::Min(DistanceSquared, FVector2D::DistSquared(Query, A + Delta * Fraction));
		bInside &= Cross(A, B, Query) >= -1.e-8;
	}
	return bInside ? 0 : FMath::Sqrt(DistanceSquared);
}

bool LSObstacleGeometry::GetClosestBodyPoint(const UPrimitiveComponent* Body, const FVector& Point, FVector& OutPoint,
											 double& OutDistance)
{
	if (!IsValid(Body) || Point.ContainsNaN())
		return false;
	if (const UBoxComponent* Box = Cast<UBoxComponent>(Body))
	{
		OutDistance = PointBoxDistance(Box, Point, &OutPoint);
		return true;
	}
	if (const USphereComponent* Sphere = Cast<USphereComponent>(Body))
	{
		const FVector Delta = Point - Sphere->GetComponentLocation();
		OutDistance = FMath::Max(0.0, Delta.Size() - Sphere->GetScaledSphereRadius());
		OutPoint = OutDistance > 0
					   ? Sphere->GetComponentLocation() + Delta.GetSafeNormal() * Sphere->GetScaledSphereRadius()
					   : Point;
		return true;
	}
	const float Distance = Body->GetClosestPointOnCollision(Point, OutPoint);
	if (Distance < 0)
		return false;
	OutDistance = Distance;
	return true;
}

double LSObstacleGeometry::GetAttackDistance(const AActor* Source, const ALSPathObstacle* Target,
											 const FVector& SourceLocation)
{
	const UCapsuleComponent* Capsule = IsValid(Source) ? Source->FindComponentByClass<UCapsuleComponent>() : nullptr;
	const UPrimitiveComponent* Body = IsValid(Target) ? Target->GetObstacleBody() : nullptr;
	if (!Capsule || !Body || SourceLocation.ContainsNaN())
		return std::numeric_limits<double>::infinity();
	const FVector Center = SourceLocation + Capsule->GetComponentLocation() - Source->GetActorLocation();
	const double Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Axis = Capsule->GetUpVector() *
						 FMath::Max(0.0f, Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius());
	if (const USphereComponent* Sphere = Cast<USphereComponent>(Body))
	{
		const FVector Closest =
			FMath::ClosestPointOnSegment(Sphere->GetComponentLocation(), Center - Axis, Center + Axis);
		return FMath::Max(0.0, FVector::Distance(Closest, Sphere->GetComponentLocation()) - Radius -
								   Sphere->GetScaledSphereRadius());
	}
	if (const UBoxComponent* Box = Cast<UBoxComponent>(Body))
	{
		// 선분을 따른 볼록 상자와의 거리는 볼록 함수다. 평평한 최솟값을 위해 끝점도 검사한다.
		double Low = -1, High = 1;
		for (int32 Index = 0; Index < 48; ++Index)
		{
			const double Left = (2 * Low + High) / 3, Right = (Low + 2 * High) / 3;
			if (PointBoxDistance(Box, Center + Axis * Left) <= PointBoxDistance(Box, Center + Axis * Right))
				High = Right;
			else
				Low = Left;
		}
		const double Distance = FMath::Min3(PointBoxDistance(Box, Center - Axis), PointBoxDistance(Box, Center + Axis),
											PointBoxDistance(Box, Center + Axis * ((Low + High) / 2)));
		return FMath::Max(0.0, Distance - Radius);
	}
	return std::numeric_limits<double>::infinity();
}

TArray<FVector> LSObstacleGeometry::GetAttackApproachCandidates(const AActor* Source, const ALSPathObstacle* Target,
																float Range)
{
	TArray<FVector> Result;
	const UCapsuleComponent* Capsule = IsValid(Source) ? Source->FindComponentByClass<UCapsuleComponent>() : nullptr;
	if (!Capsule || !IsValid(Target) || !Target->GetObstacleBody() || Range <= 0)
		return Result;
	FVector Closest;
	double Distance;
	if (!GetClosestBodyPoint(Target->GetObstacleBody(), Capsule->GetComponentLocation(), Closest, Distance))
		return Result;
	FVector Direction = (Capsule->GetComponentLocation() - Closest).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		Direction = FVector::ForwardVector;
	const double Offset = Capsule->GetScaledCapsuleRadius() + Range * .65;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const FVector Ray = Direction.RotateAngleAxis(Index * 22.5, FVector::UpVector);
		FVector Surface;
		const FVector Probe = Closest + Ray * (Target->GetObstacleBody()->Bounds.SphereRadius * 2 + Offset);
		if (!GetClosestBodyPoint(Target->GetObstacleBody(), Probe, Surface, Distance))
			continue;
		FVector Candidate = Surface + Ray * Offset;
		Candidate.Z = Source->GetActorLocation().Z;
		Result.Add(Candidate);
	}
	return Result;
}
