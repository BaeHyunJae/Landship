#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LSRTSHUD.generated.h"

enum class ELSCastleWeapon : uint8;
class ULSMinimapWidget;
class ULSBattleHUDWidget;
class SWidget;

/** 선택과 명령 상태를 확인하기 위한 기본 HUD. 최종 아트·위젯과 분리한다. */
UCLASS()
class LANDSHIP_API ALSRTSHUD : public AHUD
{
	GENERATED_BODY()
  public:
	ALSRTSHUD();
	virtual void DrawHUD() override;
	bool IsOverPanel(const FVector2D& Mouse) const;
	bool HandleClick(const FVector2D& Mouse);
	FBox2D GetCommandCellRect(int32 Index) const;
	bool GetMinimapWorldPoint(FVector& OutLocation) const;
	bool IsOverMinimap(const FVector2D& Mouse) const;
	bool IsMinimapPointerCaptured() const;
	bool IsMinimapWidget(const TSharedRef<SWidget>& Widget) const;
	bool IsBattleWidget(const TSharedRef<SWidget>& Widget) const;
	ULSBattleHUDWidget* GetBattleWidget() const { return BattleWidget; }

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Vision")
	TSubclassOf<ULSMinimapWidget> MinimapWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<ULSMinimapWidget> MinimapWidget;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|HUD")
	TSoftClassPtr<ULSBattleHUDWidget> BattleWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<ULSBattleHUDWidget> BattleWidget;

  private:
	FBox2D InfoPanel() const;
	FBox2D CommandPanel() const;
	void DrawSelectionRing(AActor* Actor, const FLinearColor& Color);
	void DrawWeaponShape(class ALSMobileCastle* Castle, ELSCastleWeapon Weapon, const FVector& Aim,
	                     const FLinearColor& Color, bool bPreview,
	                     const struct FLSCastleWeaponSettings* Snapshot = nullptr);
	void DrawWorldLine(const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness = 2);
};
