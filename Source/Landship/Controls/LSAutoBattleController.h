#pragma once

#include "GameFramework/PlayerController.h"
#include "Troops/LSTroopUnit.h"
#include "Match/LSAutoBattleGameMode.h"
#include "Castle/LSCastleWeaponData.h"
#include "LSAutoBattleController.generated.h"

class ULSAutoBattleWidget;

/** 준비 중 편성을 편집하고 전투 중 아군 성의 주행과 무장을 제어한다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleController : public APlayerController
{
	GENERATED_BODY()
  public:
	ALSAutoBattleController();
	void SetEditingFaction(ELSFaction Faction);
	ELSFaction GetEditingFaction() const
	{
		return EditingFaction;
	}
	void SavePreset(const FString& Name, bool bOverwrite);
	void LoadPreset(const FString& Name);
	const FString& GetEditorMessage() const
	{
		return EditorMessage;
	}
	void ChooseTroop(ELSTroopType Type);
	void ChooseCastle();
	void ChooseWeapon(ELSCastleWeapon Weapon);
	TOptional<ELSCastleWeapon> GetArmedWeapon() const { return ArmedWeapon; }
	ELSWeaponCastResult GetLastWeaponResult() const { return LastWeaponResult; }
	void BeginBattle();
	void Retry();
	void ClearFormation();
	void ToggleEraseBrush();
	bool IsErasing() const
	{
		return bEraseBrush;
	}
	ELSTroopType GetChosenTroop() const
	{
		return ChosenTroop;
	}
	int32 GetSelectionId() const
	{
		return SelectionId;
	}
	bool IsChoosingCastle() const
	{
		return bCastlePlacement;
	}
	bool IsCameraCaptured() const
	{
		return bCameraCaptured;
	}
	ELSAutoBattleRequest GetLastRequest() const
	{
		return LastRequest;
	}
	ULSAutoBattleWidget* GetBattleWidget() const
	{
		return Widget;
	}
	bool GetGroundUnderCursor(FVector& OutGround) const;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSoftClassPtr<ULSAutoBattleWidget> WidgetClass;
	UPROPERTY(EditDefaultsOnly, Category = "Placement", meta = (ClampMin = "0.02", Units = "s"))
	float PreviewRefreshInterval = 0.05f;
	/** 지면 위 생성 간격(cm). 기마병 지름과 배치 여유보다 크게 둔다. */
	UPROPERTY(EditDefaultsOnly, Category = "Placement|Brush", meta = (ClampMin = "140", Units = "cm"))
	float BrushSpacing = 150.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Placement|Brush", meta = (ClampMin = "1", Units = "cm"))
	float EraseRadius = 65.0f;

  private:
	friend class ALSAutoBattleProbe;
	void LeftClick();
	void EndBrushStroke();
	bool CanUseBrush() const;
	void BeginBrushStroke(const FVector& Ground);
	void ContinueBrushStroke(const FVector& Ground);
	void PaintBrushSample(const FVector& Ground);
	void EraseBrushSegment(const FVector& Start, const FVector& End);
	void StartHotkey();
	void LookPressed();
	void LookReleased();
	void PanPressed();
	void PanReleased();
	void Escape();
	void Home();
	void WheelUp();
	void WheelDown();
	void SetCameraCapture(bool bCapture);
	void ArmCannon();
	void ArmShotgun();
	void ArmBallista();
	void UpdateCastleAim(const ALSAutoBattleCastle& Castle) const;
	void ConfirmCastleWeapon(ALSAutoBattleCastle& Castle);
	UFUNCTION()
	void OnBattleChanged();
	UPROPERTY(Transient)
	TObjectPtr<ULSAutoBattleWidget> Widget;
	ELSTroopType ChosenTroop = ELSTroopType::Spearman;
	ELSAutoBattleRequest LastRequest = ELSAutoBattleRequest::Success;
	TOptional<ELSCastleWeapon> ArmedWeapon;
	ELSWeaponCastResult LastWeaponResult = ELSWeaponCastResult::Ready;
	ELSAutoBattlePhase LastPhase = ELSAutoBattlePhase::Loading;
	int32 SelectionId = 0;
	int32 LastBattleId = 0;
	ELSFaction EditingFaction = ELSFaction::Ally;
	FString EditorMessage;
	bool bCastlePlacement = false;
	bool bCameraCaptured = false;
	bool bPanning = false;
	bool bHasCaptureCursor = false;
	FVector2D CaptureCursor = FVector2D::ZeroVector;
	double NextPreviewTime = 0;
	bool bPreviewValid = false;
	bool bEraseBrush = false;
	bool bBrushStroke = false;
	FVector PreviousBrushGround = FVector::ZeroVector;
	double BrushTravel = 0;
};
