#include "HUD/LSCommandButton.h"
#include "HUD/LSBattleHUDWidget.h"

void ULSCommandButton::BindCell(ULSBattleHUDWidget* Widget, int32 Index)
{
	OwnerWidget = Widget;
	Cell = Index;
	OnClicked.AddUniqueDynamic(this, &ThisClass::ExecuteCell);
}
void ULSCommandButton::ExecuteCell()
{
	if (OwnerWidget.IsValid())
		OwnerWidget->ExecuteCommandCell(Cell);
}
