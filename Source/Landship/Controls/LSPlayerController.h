#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSTroopProductionComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "LSPlayerController.generated.h"

class ALSCastle;
class ALSMobileCastle;

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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Production")
	TObjectPtr<ULSTroopProductionComponent> Production;
	void ProduceTroop(ELSTroopType Type);
	ALSMobileCastle* GetSelectedArmedCastle() const;
	bool ArmCastleWeapon(ELSCastleWeapon Weapon);
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
	void SetPendingInput(ELSCommandInput Input)
	{
		CancelInput();
		PendingInput = Input;
	}
	void ArmAttack();
	void ArmMove();
	void StopSelected();
	void HoldSelected();
	void CancelInput();
	void FocusSelection();
	bool GetOrderMarker(FVector& OutLocation, FLinearColor& OutColor) const;

  protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

  private:
	void OnSelectPressed();
	void OnSelectReleased();
	void SelectSameType();
	void OnRightClick();
	void CompleteTargetInput();
	UFUNCTION()
	void OnAimedWeaponStateChanged(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State);
	bool IsShiftDown() const;
	bool HitWorld(FHitResult& Hit) const;
	void ZoomCamera(float Value);
	bool CanUseWorldInput(bool bAllowHUD = false) const;

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
	TWeakObjectPtr<ALSMobileCastle> AimingCastle;
	ELSProductionResult LastProductionResult = ELSProductionResult::Ready;
	int32 LastGroup = INDEX_NONE;
	double LastGroupTime = -1;
	FVector OrderMarkerLocation = FVector::ZeroVector;
	FLinearColor OrderMarkerColor = FLinearColor::Green;
	double OrderMarkerExpiresAt = -1;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	UPROPERTY(Transient)
	TMap<int32, FLSControlGroup> ControlGroups;
};
