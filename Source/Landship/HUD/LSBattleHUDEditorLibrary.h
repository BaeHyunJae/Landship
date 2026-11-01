#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSBattleHUDEditorLibrary.generated.h"

/** 개발용 최초 WBP 생성기. 이미 존재하는 디자이너 편집 결과는 덮어쓰지 않는다. */
UCLASS()
class LANDSHIP_API ULSBattleHUDEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable, Category = "Landship|Editor")
	static bool CreateBattleHUDAsset();
};
