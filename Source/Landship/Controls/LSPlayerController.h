#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSTroopProductionComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "LSPlayerController.generated.h"

class ULSCommandCardComponent;
class ALSCastle;
class ALSMine;
class ALSPathObstacle;

USTRUCT()
struct FLSControlGroup
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Actors;
};

UENUM()
enum class ELSCommandInput : uint8
{
	None,
	Move,
	Attack,
	CastleWeapon
};

/** RTS 카메라 입력, 공통 선택과 부대 지정 및 선택 대상의 명령 전달을 처리한다. */
UCLASS()
class LANDSHIP_API ALSPlayerController : public APlayerController
{
	GENERATED_BODY()
  public:
	ALSPlayerController();
	/** 기존 선택을 유지하고 준비 입력·미니맵 드래그·명령 표식을 정리한다. */
	void EndBattleActions();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Commands")
	TObjectPtr<ULSCommandCardComponent> CommandCard;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Production")
	TObjectPtr<ULSTroopProductionComponent> Production;
	void ProduceTroop(ELSTroopType Type);
	ALSCastle* GetSelectedArmedCastle() const;
	/** 전장 설정이 유효할 때 지정된 성을 반환한다. 사망한 성도 체력 표시를 위해 반환한다. */
	ALSCastle* GetConfiguredBattlefieldCastle() const;
	bool UsesDirectCastleControl() const { return bRequiresBattlefieldCastle; }
	/** 무장 입력에 사용할 살아 있는 성. 전장 설정을 쓰지 않는 기존 맵에서만 선택한 성을 사용한다. */
	ALSCastle* GetCastleWeaponSource() const;
	ALSCastle* GetAimingCastle() const;
	UFUNCTION(BlueprintCallable, Category = "Landship|Castle Weapons")
	bool ArmCastleWeapon(ELSCastleWeapon Weapon);
	UFUNCTION(BlueprintCallable, Category = "Landship|Castle Weapons")
	ELSWeaponCastResult ConfirmCastleWeapon(const FVector& Aim);
	bool GetCastleWeaponAim(FVector& Aim) const;
	ELSCastleWeapon GetPendingWeapon() const
	{
		return PendingWeapon;
	}
	ELSWeaponCastResult GetLastWeaponResult() const
	{
		return LastWeaponResult;
	}
	ELSProductionResult GetLastProductionResult() const
	{
		return LastProductionResult;
	}
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	ALSCastle* GetSelectedCastle() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	TArray<AActor*> GetSelectedActors() const;
	void ApplySelection(const TArray<AActor*>& Actors, bool bToggle = false, bool bPreferUnits = false);
	void UseControlGroup(int32 Number, bool bAssign);
	bool IssueSelectionOrder(ELSOrderKind Kind, const FVector& Location, AActor* Target, bool bQueue);
	bool GetSelectionBox(FVector2D& Start, FVector2D& End) const;
	ELSCommandInput GetPendingInput() const
	{
		return PendingInput;
	}
	void SetPendingInput(ELSCommandInput Input);
	/** 진행 중에는 미니맵 좌표로 명령을 전달하고 일반 좌클릭을 카메라에 맡긴다. 종료 뒤에는 모두 소비한다. */
	bool HandleMinimapInput(const FVector& Location, bool bRightClick, bool bQueue);
	void ArmAttack();
	void ArmMove();
	void StopSelected();
	void HoldSelected();
	void CancelInput();
	void FocusSelection();
	bool GetOrderMarker(FVector& OutLocation, FLinearColor& OutColor) const;
	AActor* GetAttackCommandMarker() const;

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

  private:
	friend class ALSObstacleReviewRuntimeProbe;
	UFUNCTION()
	void PruneSelection();
	void OnSelectPressed();
	void OnSelectReleased();
	void SelectSameType();
	void OnRightClick();
	void CompleteTargetInput();
	UFUNCTION()
	void OnAimedWeaponStateChanged(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State);
	bool IsShiftDown() const;
	bool HitWorld(FHitResult& Hit, bool bNormalRightClick = false) const;
	void ZoomCamera(float Value);
	bool CanUseWorldInput(bool bAllowHUD = false, bool bKeyboard = false) const;
	bool UsesBattlefieldCastle() const;
	// 설정 액터가 제거되어도 기존 선택 기반 입력으로 복귀하지 않는다.
	mutable bool bRequiresBattlefieldCastle = false;
	bool bControllerEnded = false;
	TWeakObjectPtr<ALSCastle> DrivenCastle;
#if WITH_EDITOR
	TSharedPtr<class IInputProcessor> PIECancelInputProcessor;
#endif

	UPROPERTY(EditDefaultsOnly, Category = "Landship|Camera", meta = (ClampMin = "0"))
	float EdgeScrollMargin = 12.0f;

	FVector2D PreviousMouse = FVector2D::ZeroVector;
	bool bWasDraggingCamera = false;
	bool bSelecting = false;
	FVector2D SelectionStart = FVector2D::ZeroVector;
	FVector2D PressedMousePosition = FVector2D::ZeroVector;
	bool bHasPressedMousePosition = false;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Selection", meta = (ClampMin = "1"))
	float DragThreshold = 6.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Selection", meta = (ClampMin = "0.1"))
	float DoubleGroupInterval = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "1"))
	float FormationSpacing = 150.0f;
	ELSCommandInput PendingInput = ELSCommandInput::None;
	ELSCastleWeapon PendingWeapon = ELSCastleWeapon::Cannon;
	ELSWeaponCastResult LastWeaponResult = ELSWeaponCastResult::Ready;
	TWeakObjectPtr<ALSCastle> AimingCastle;
	mutable TWeakObjectPtr<ALSPathObstacle> AimedObstacle;
	ELSProductionResult LastProductionResult = ELSProductionResult::Ready;
	int32 LastGroup = INDEX_NONE;
	double LastGroupTime = -1;
	FVector OrderMarkerLocation = FVector::ZeroVector;
	FLinearColor OrderMarkerColor = FLinearColor::Green;
	double OrderMarkerExpiresAt = -1;
	TWeakObjectPtr<ALSMine> HoveredMine;
	TWeakObjectPtr<AActor> AttackCommandTarget;
	double AttackCommandMarkerExpiresAt = -1;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0"))
	float AttackCommandMarkerDuration = 0.7f;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	UPROPERTY(Transient)
	TMap<int32, FLSControlGroup> ControlGroups;
};
