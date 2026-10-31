#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LSRTSHUD.generated.h"

enum class ELSCastleWeapon : uint8;

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
	FBox2D WeaponPanel() const;
	FBox2D WeaponButton(int32 Index) const;
	void DrawWeaponShape(class ALSMobileCastle* Castle, ELSCastleWeapon Weapon, const FVector& Aim,
	                     const FLinearColor& Color, bool bPreview,
	                     const struct FLSCastleWeaponSettings* Snapshot = nullptr);
	void DrawWorldLine(const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness = 2);
};
