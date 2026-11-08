#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSShellSupplyEditorLibrary.generated.h"

UCLASS()
class LANDSHIP_API ULSShellSupplyEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 새 Supply WBP를 저작한다. 전체 모듈 요청은 4열 2행 선택판과 두 줄 조합 설명을 보완한다.
	 * 완성된 선택판에 설명을 추가할 때 기존 컨트롤의 화면 위치는 보존한다.
	 * 잘못된 기존 에셋 또는 런타임 호출은 null을 반환한다. 호출자가 반환된 에셋을 저장한다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static UObject* BuildSupplyWidgetAsset(bool bIncludeElectric = false, bool bIncludeAll = false);
	/** 에디터 전장의 바닥에 공통 지면·폭발 차폐 채널 계약을 적용하고 결과를 확인한다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static bool ConfigureFloorCollision(class UPrimitiveComponent* Floor);
	/** 에디터 바닥의 지면·폭발 차폐 채널과 지정 물리 재질 참조를 읽기 전용으로 검사한다.
	 * 일치하면 빈 문자열, 불일치 또는 런타임 호출이면 원인을 반환한다. */
	UFUNCTION(BlueprintCallable, Category = "Shell Experiment|Editor")
	static FString GetFloorPhysicsError(class UPrimitiveComponent* Floor, class UPhysicalMaterial* ExpectedMaterial);
};
