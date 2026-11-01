#pragma once

#include "Components/Button.h"
#include "LSCommandButton.generated.h"

class ULSBattleHUDWidget;

UCLASS()
class LANDSHIP_API ULSCommandButton : public UButton
{
	GENERATED_BODY()
  public:
	void BindCell(ULSBattleHUDWidget* Widget, int32 Index);

  private:
	UFUNCTION() void ExecuteCell();
	TWeakObjectPtr<ULSBattleHUDWidget> OwnerWidget;
	int32 Cell = INDEX_NONE;
};
