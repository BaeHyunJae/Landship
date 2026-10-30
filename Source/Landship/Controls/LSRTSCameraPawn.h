#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "LSRTSCameraPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;

/** 성이나 병력의 이동과 독립적으로 전장을 관찰한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSRTSCameraPawn : public APawn
{
	GENERATED_BODY()
public:
	ALSRTSCameraPawn();

	void Pan(const FVector2D& ScreenDirection, float DeltaSeconds);
	void Drag(const FVector2D& MouseDelta);
	void Zoom(float WheelDelta);

	UFUNCTION(BlueprintCallable, Category = "Landship|Camera")
	void FocusLocation(const FVector& Location);

	UFUNCTION(BlueprintPure, Category = "Landship|Camera")
	float GetZoomDistance() const;

	/** 화면 밖의 커서에서는 0을 반환한다. UI·포커스 여부는 컨트롤러에서 확인한다. */
	static FVector2D CalculateEdgeDirection(const FVector2D& Mouse, const FVector2D& ViewportSize, float Margin);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "0"))
	float PanSpeed = 1800.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "0"))
	float DragUnitsPerPixel = 2.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "0"))
	float ZoomStep = 200.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "1"))
	float MinZoomDistance = 700.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "1"))
	float MaxZoomDistance = 4500.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera")
	float FocusHeight = 0.0f;
	/** 지도의 이동 가능한 초점 범위다. 화면의 모든 모서리가 맵 안에 들어간다는 의미는 아니다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera")
	FVector2D BoundsMin = FVector2D(-2400, -2000);
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera")
	FVector2D BoundsMax = FVector2D(2400, 2000);

private:
	FVector ScreenToWorldDirection(const FVector2D& Direction) const;
};
