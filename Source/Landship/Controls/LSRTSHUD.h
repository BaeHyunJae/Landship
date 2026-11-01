#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LSRTSHUD.generated.h"

class ULSMinimapWidget;
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
	bool IsOverMinimap(const FVector2D& Mouse) const;
	bool IsMinimapPointerCaptured() const;
	bool IsMinimapWidget(const TSharedRef<SWidget>& Widget) const;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Vision")
	TSubclassOf<ULSMinimapWidget> MinimapWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<ULSMinimapWidget> MinimapWidget;

  private:
	FBox2D InfoPanel() const;
	FBox2D ProductionPanel() const;
	FBox2D ProductionButton(int32 Index) const;
};
