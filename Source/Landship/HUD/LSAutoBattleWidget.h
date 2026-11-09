#pragma once

#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "LSAutoBattleWidget.generated.h"

/** 배치·무장·전투 요청만 전달한다. WBP가 화면 배치와 디자인을 소유한다. */
UCLASS()
class LANDSHIP_API ULSAutoBattleWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	void RefreshState();
	bool IsPointerOverPanel() const;
	bool IsEditingText() const;
	void RefreshPresets();
	void SelectPreset(const FString& Name);

  protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

  private:
	void SetLabel(const TCHAR* Name, const FString& Text);
	void EnsureTroopButtons();
	void SetupFieldButtons();
	UFUNCTION() void SwampField();
	UFUNCTION() void StimField();
	UFUNCTION() void WallField();
	void SetupBattlefieldPicker();
	UFUNCTION() void OnBattlefieldSelected(FString Selection, ESelectInfo::Type SelectionType);
	UFUNCTION() void Spear();
	UFUNCTION() void Archer();
	UFUNCTION() void Cavalry();
	UFUNCTION() void Medic();
	UFUNCTION() void Shieldbearer();
	UFUNCTION() void Mortar();
	UFUNCTION() void Bomber();
	UFUNCTION() void Castle();
	UFUNCTION() void Cannon();
	UFUNCTION() void Shotgun();
	UFUNCTION() void Ballista();
	UFUNCTION() void Start();
	UFUNCTION() void Retry();
	UFUNCTION() void Reset();
	UFUNCTION() void Remove();
	UFUNCTION() void EditAllies();
	UFUNCTION() void EditEnemies();
	UFUNCTION() void SaveNewPreset();
	UFUNCTION() void OverwritePreset();
	UFUNCTION() void LoadSelectedPreset();
	UFUNCTION() void ReloadPresetList();
	FTimerHandle StatusTimer;
};
