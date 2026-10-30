#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Controls/LSOrderComponent.h"
#include "LSPlayerController.generated.h"

class ALSCastle;

USTRUCT()
struct FLSControlGroup
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Actors;
};

UENUM()
enum class ELSCommandInput : uint8 { None, Move, Attack };

/** 성 좌클릭 선택과 지면 우클릭 이동 명령을 처리한다. */
UCLASS()
class LANDSHIP_API ALSPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ALSPlayerController();
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	ALSCastle* GetSelectedCastle() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	TArray<AActor*> GetSelectedActors() const;
	void ApplySelection(const TArray<AActor*>& Actors, bool bToggle = false, bool bPreferUnits = false);
	void UseControlGroup(int32 Number, bool bAssign);
	bool IssueSelectionOrder(ELSOrderKind Kind, const FVector& Location, AActor* Target, bool bQueue);
	bool GetSelectionBox(FVector2D& Start, FVector2D& End) const;
	ELSCommandInput GetPendingInput() const { return PendingInput; }
	void SetPendingInput(ELSCommandInput Input) { CancelInput(); PendingInput = Input; }
	void ArmAttack();
	void ArmMove();
	void StopSelected();
	void HoldSelected();
	void CancelInput();
	void FocusSelection();

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
	int32 LastGroup = INDEX_NONE;
	double LastGroupTime = -1;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	UPROPERTY(Transient)
	TMap<int32, FLSControlGroup> ControlGroups;
};
