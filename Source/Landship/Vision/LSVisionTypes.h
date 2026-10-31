#pragma once

#include "CoreMinimal.h"
#include "LSVisionTypes.generated.h"

UENUM(BlueprintType)
enum class ELSMinimapMarkerKind : uint8
{
	Unit,
	Castle,
	Building,
	Obstacle
};

/** 한 번의 시야 갱신에서 확정한 원이다. 지형 높이와 관계없이 XY만 사용한다. */
struct FLSSightCircle
{
	FVector2D Center = FVector2D::ZeroVector;
	double Radius = 0.0;
};

USTRUCT(BlueprintType)
struct FLSMinimapMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	ELSMinimapMarkerKind Kind = ELSMinimapMarkerKind::Unit;
	UPROPERTY(BlueprintReadOnly)
	uint8 bFriendly : 1 = false;
	UPROPERTY(BlueprintReadOnly)
	uint8 bPublicPosition : 1 = false;
};
