#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSShellExperimentEditorLibrary.generated.h"

/** 실험 전용 UI 에셋을 에디터에서 저작한다. 런타임 위젯 트리를 만들지 않는다. */
UCLASS()
class LANDSHIP_API ULSShellExperimentEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category = "Shell Experiment|Editor")
	static FString GetConfiguredNavigationClass(UObject* WorldContext);
	/** 비어 있는 실험 WBP를 채우고 반환한다. 기존 배치가 유효하면 보존하며 오류 또는 런타임에서는 null이다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static UObject* BuildWidgetAsset();
	/** 기존 실험 WBP의 배치를 유지하며 반응 설명과 글자 그림자만 추가한다. 호출자가 에셋을 저장한다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static UObject* UpgradeFeedbackWidgetAsset();
	/** 기존 실험 WBP에 없는 슬롯별 선택 버튼과 분열 상태만 추가하며 기존 배치는 보존한다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static UObject* UpgradeP1WidgetAsset();
};
