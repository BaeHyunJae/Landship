#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LSShellExperimentController.generated.h"

class ALSShellExperimentCoordinator;
class UUserWidget;

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellExperimentController : public APlayerController
{
	GENERATED_BODY()
public:
	ALSShellExperimentController();
	virtual void PlayerTick(float DeltaSeconds) override;
	ALSShellExperimentCoordinator* GetCoordinator() const;
	bool GetAimPoint(FVector& OutPoint) const;
	UFUNCTION(BlueprintPure) FString GetAimHint() const;
	UFUNCTION(BlueprintPure) bool CanEditComposition() const;
protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|UI")
	TSubclassOf<UUserWidget> WidgetClass;
private:
	bool HasGameFocus() const;
	bool CanUseWorldPointer() const;
	bool TraceAim(FHitResult& OutHit) const;
	void Fire();
	void ToggleCompose();
	void EndCompose();
	void CancelCompose();
	void SwapModules();
	void BeginDrag();
	void EndDrag();
	void ZoomIn();
	void ZoomOut();
	void Zoom(float Direction);
	void TriggerPending();
	void RecallMaterial();
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ExperimentWidget;
	TWeakObjectPtr<AActor> AimTarget;
	FVector AimPoint = FVector::ZeroVector;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	bool bHasAim = false;
	bool bDragging = false;
};
