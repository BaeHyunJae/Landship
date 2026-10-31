#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LSRTSHUD.generated.h"

/** 선택과 명령 상태를 확인하기 위한 기본 HUD. 최종 아트·위젯과 분리한다. */
UCLASS()
class LANDSHIP_API ALSRTSHUD : public AHUD
{
	GENERATED_BODY()
  public:
	virtual void DrawHUD() override;
	bool IsOverPanel(const FVector2D& Mouse) const;
	bool HandleClick(const FVector2D& Mouse);

  private:
	FBox2D InfoPanel() const;
	FBox2D ProductionPanel() const;
	FBox2D ProductionButton(int32 Index) const;
};
