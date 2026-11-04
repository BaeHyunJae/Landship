#pragma once

#include "GameFramework/PlayerController.h"
#include "Troops/LSTroopUnit.h"
#include "Match/LSAutoBattleGameMode.h"
#include "LSAutoBattleController.generated.h"

class ULSAutoBattleWidget;

/** 준비 중 배치만 요청하고 전투 중에는 관전 카메라만 조작한다. RTS 명령 입력은 상속하지 않는다. */
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
	void SetLimits(int32 Budget, int32 Limit);
	const FString& GetEditorMessage() const
	{
		return EditorMessage;
	}
	void ChooseTroop(ELSTroopType Type);
	void ChooseCastle();
	void ChooseWeapon(ELSCastleWeapon Weapon);
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
	UFUNCTION()
	void OnBattleChanged();
	UPROPERTY(Transient)
	TObjectPtr<ULSAutoBattleWidget> Widget;
	ELSTroopType ChosenTroop = ELSTroopType::Spearman;
	ELSAutoBattleRequest LastRequest = ELSAutoBattleRequest::Success;
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
