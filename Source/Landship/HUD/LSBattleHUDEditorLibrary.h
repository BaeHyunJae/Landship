#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSBattleHUDEditorLibrary.generated.h"

/** 개발용 WBP 생성기. 기존 디자인을 교체하려면 명시적인 재생성 요청을 사용한다. */
UCLASS()
class LANDSHIP_API ULSBattleHUDEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintCallable, Category = "Landship|Editor")
	static bool CreateBattleHUDAsset();
	/** 기존 WBP의 위젯 트리를 기본 배치로 교체하고 저장한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Editor")
	static bool RebuildBattleHUDAsset();

  private:
	static bool BuildBattleHUDAsset(bool bRebuild);
};
