#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LSShellExperimentTypes.h"
#include "LSShellExperimentWidget.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;

UCLASS(Blueprintable)
class LANDSHIP_API ULSShellExperimentWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable) void RefreshState();
	UFUNCTION(BlueprintCallable) void RestartExperiment();
	UFUNCTION(BlueprintCallable) void SwapModules();
	UFUNCTION(BlueprintCallable) void SelectModule(int32 SlotIndex, ELSShellModule Module);
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HealthText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CoreText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> LoadoutText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ReactionText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HelpText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SplitStatusText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> ReloadProgress;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> RestartButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SwapButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot1GravityButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot1BlastButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot1SplitButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot2GravityButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot2BlastButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot2SplitButton;
private:
	UFUNCTION() void SelectSlot1Gravity();
	UFUNCTION() void SelectSlot1Blast();
	UFUNCTION() void SelectSlot1Split();
	UFUNCTION() void SelectSlot2Gravity();
	UFUNCTION() void SelectSlot2Blast();
	UFUNCTION() void SelectSlot2Split();
};
