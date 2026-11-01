#pragma once

#include "CoreMinimal.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "LSBattlefieldStateTestObserver.generated.h"

/** 동적 함락 알림의 호출 시점과 횟수를 관찰한다. */
UCLASS()
class ULSBattlefieldStateTestObserver : public UObject
{
	GENERATED_BODY()
public:
	TArray<FLSOutpostCaptureSnapshot> Captures;
	TFunction<void(const FLSOutpostCaptureSnapshot&)> CaptureCallback;
	UFUNCTION()
	void OnCaptured(const FLSOutpostCaptureSnapshot& Capture);
};
