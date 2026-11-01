#pragma once

#include "Controls/LSCommandProviderComponent.h"
#include "LSCommandCardComponent.generated.h"

struct FLSCommandCell
{
	FLSCommandEntry Entry;
	TWeakObjectPtr<ULSCommandProviderComponent> Provider;
	TWeakObjectPtr<AActor> Source;
};

/** 선택된 병종의 명령을 고정된 5열 3행 격자로 변환한다. */
UCLASS()
class LANDSHIP_API ULSCommandCardComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	static constexpr int32 CellCount = 15;
	TArray<FLSCommandCell> BuildCard();
	bool ExecuteCell(int32 Cell, const FLSCommandCell& Expected);
	bool HandleHotkey(FKey Key);
	void CycleGroup();
	FText GetGroupText();

  private:
	TArray<AActor*> GetActiveActors();
	FName ActiveType;
};
