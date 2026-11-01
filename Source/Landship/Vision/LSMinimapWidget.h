#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LSMinimapWidget.generated.h"

/** 패널 그리기와 입력이 같은 좌표를 사용하도록 함께 배치한다. */
struct LANDSHIP_API FLSBattlefieldHUDLayout
{
	FBox2D Minimap;
	FBox2D Info;
	FBox2D Production;
	float Scale = 1.0f;
	static FLSBattlefieldHUDLayout Calculate(FVector2D Viewport, bool bMinimap, float MinimapSide = 260.0f);
};

/** WBP 외형에 현재 전장 정보와 카메라 이동 입력을 제공한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ULSMinimapWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	/** 기존 HUD와 공유할 패널 크기이며 WBP 기본값에서 조정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Vision|Layout",
			  meta = (ClampMin = "64"))
	float PanelSide = 260.0f;
	bool IsPointerCaptured() const
	{
		return bDragging;
	}

  protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
							  const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
							  const FWidgetStyle& Style, bool bEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;

  private:
	bool FocusCamera(const FGeometry& Geometry, FVector2D ScreenPosition, bool bClampToMap = false);
	bool bDragging = false;
};
