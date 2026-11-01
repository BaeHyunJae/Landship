#pragma once

#include "Blueprint/UserWidget.h"
#include "Controls/LSCommandCardComponent.h"
#include "LSBattleHUDWidget.generated.h"

class UTextBlock;
class UButton;
class UProgressBar;
class UWidget;

/** WBP가 외형을 소유하고 이 클래스는 전투 조회와 기존 요청 전달만 수행한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ULSBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	void RefreshState();
	bool ExecuteCommandCell(int32 Index);
	void SetPanelLayout(const FBox2D& Info, const FBox2D& Commands);
	UFUNCTION(BlueprintPure, Category = "Landship|HUD")
	FText GetSelectionDescription() const;
	UFUNCTION(BlueprintPure, Category = "Landship|HUD")
	FText GetCommandFailureText() const;

  protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
	                          FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
	                          bool bEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|HUD", meta = (ClampMin = "0.1"))
	float DiscoveryDuration = 3.0f;

  private:
	TArray<FLSCommandCell> DisplayedCells;
	void SetText(const TCHAR* Name, const FText& Value);
	bool bDiscoveryVisible = false;
	mutable bool bDiscoveryPainted = false;
	mutable double DiscoveryStart = 0;
};
