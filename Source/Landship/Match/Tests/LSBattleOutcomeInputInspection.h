#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "LSBattleOutcomeInputInspection.generated.h"

class APlayerController;

/** 실제 Slate/OS 입력 전후의 게임 상태와 창 좌표만 읽는 개발 검사다. */
UCLASS()
class ULSBattleOutcomeInputInspection : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, meta = (DevelopmentOnly))
	static FString Snapshot(APlayerController* Controller);
};
