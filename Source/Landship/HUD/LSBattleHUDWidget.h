#pragma once

#include "Blueprint/UserWidget.h"
#include "Controls/LSCommandCardComponent.h"
#include "Castle/LSCastleWeaponData.h"
#include "LSBattleHUDWidget.generated.h"

class UTextBlock;
class UButton;
class UProgressBar;
class UWidget;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCastleWeaponHUDState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Landship|HUD")
	FText Title;
	UPROPERTY(BlueprintReadOnly, Category = "Landship|HUD")
	FText Status;
	UPROPERTY(BlueprintReadOnly, Category = "Landship|HUD")
	float Remaining = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Landship|HUD")
	float Duration = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Landship|HUD")
	bool bAvailable = false;
};

/** WBP가 외형을 소유하고 이 클래스는 전투 조회와 기존 요청 전달만 수행한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ULSBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	void RefreshState();
	bool ExecuteCommandCell(int32 Index);
	void SetPanelLayout(const FBox2D& Info, const FBox2D& Commands);
	void SetCastleStatusLayout(const FBox2D& Status);
	UFUNCTION(BlueprintPure, Category = "Landship|HUD")
	FText GetCastleHealthText() const;
	UFUNCTION(BlueprintPure, Category = "Landship|HUD")
	float GetCastleHealthFraction() const;
	UFUNCTION(BlueprintPure, Category = "Landship|HUD")
	FLSCastleWeaponHUDState GetCastleWeaponStatus(ELSCastleWeapon Weapon) const;
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
