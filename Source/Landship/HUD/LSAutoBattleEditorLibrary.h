#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSAutoBattleEditorLibrary.generated.h"

/** 개인 자동 전투 WBP의 최초 생성과 명시적 편집 패널 추가를 제공한다. */
UCLASS()
class LANDSHIP_API ULSAutoBattleEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable, Category = "Landship|Editor")
	static bool CreateAutoBattleWidget();
	/** 기존 개인 WBP의 원래 패널을 유지하고 시나리오 편집 패널만 추가한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Editor")
	static bool UpgradeAutoBattleWidget();
};
