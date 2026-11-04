#pragma once

#include "GameFramework/Pawn.h"
#include "LSFreeCameraPawn.generated.h"

class USphereComponent;
class UCameraComponent;

/** 병력에 빙의하지 않는 자유 관전 카메라. 이동 입력과 커서 모드는 컨트롤러가 소유한다. */
UCLASS()
class LANDSHIP_API ALSFreeCameraPawn : public APawn
{
	GENERATED_BODY()
  public:
	ALSFreeCameraPawn();
	void MoveCamera(const FVector& LocalDirection, float DeltaSeconds, bool bFast);
	void Look(const FVector2D& MouseDelta);
	void Pan(const FVector2D& MouseDelta);
	void Dolly(float Steps);
	void ChangeSpeed(float Steps);
	void ResetView();
	float GetMoveSpeed() const
	{
		return MoveSpeed;
	}

  protected:
	virtual void BeginPlay() override;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Body;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "1"))
	float MoveSpeed = 1000;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "1"))
	float MinSpeed = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "1"))
	float MaxSpeed = 5000;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "1"))
	float FastMultiplier = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "0.01"))
	float LookSensitivity = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "0.01"))
	float PanUnitsPerPixel = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "1", Units = "cm"))
	float DollyStep = 200.0f;

  private:
	FVector BoundsMin = FVector(-2500, -2000, 40);
	FVector BoundsMax = FVector(2500, 2000, 3000);
	FVector HomeLocation = FVector(-2000, -1600, 1800);
	FRotator HomeRotation = FRotator(-38, 35, 0);
};
