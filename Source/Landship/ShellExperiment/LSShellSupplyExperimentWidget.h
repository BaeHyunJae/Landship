#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LSShellExperimentTypes.h"
#include "LSShellSupplyExperimentWidget.generated.h"

class UButton;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;

UCLASS(Blueprintable)
class LANDSHIP_API ULSShellSupplyExperimentWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable) void RefreshState();
	UFUNCTION(BlueprintCallable) void RestartExperiment();
	UFUNCTION(BlueprintCallable) void SwapModules();
	UFUNCTION(BlueprintCallable) void SelectModule(ELSShellModule Module);
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> CastleHealthProgress;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> CoreHealthProgress;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CastleHealthText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CoreHealthText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Slot1Text;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Slot2Text;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ReloadProgress;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PendingText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> FailureText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StateText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> WarningText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ResourceText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CombinationText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> CompositionPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Slot1Button;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> Slot2Button;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BlastButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ThrustButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ElectricButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> GravityButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SplitButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> IronButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> AdhesionButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CoolingButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SwapButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RestartButton;
private:
	UFUNCTION() void SelectSlot1();
	UFUNCTION() void SelectSlot2();
	UFUNCTION() void SelectBlast();
	UFUNCTION() void SelectThrust();
	UFUNCTION() void SelectElectric();
	UFUNCTION() void SelectGravity();
	UFUNCTION() void SelectSplit();
	UFUNCTION() void SelectIron();
	UFUNCTION() void SelectAdhesion();
	UFUNCTION() void SelectCooling();
	int32 ActiveSlot = 0;
};
