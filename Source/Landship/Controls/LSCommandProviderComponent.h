#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "LSCommandProviderComponent.generated.h"

class ALSPlayerController;

/** 한 칸의 표시 정보. 실행 가능 여부와 쿨타임의 원본은 제공자가 소유한다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSCommandEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Detail;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Tooltip;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FKey Hotkey;
	/** 0~4는 공통 명령용으로 예약한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Cell = INDEX_NONE;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Remaining = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Duration = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 0;
};

/** 병종별 스킬을 UI와 분리한다. 대상 지정형은 ExecuteCommand에서 자체 조준 상태로 진입한다. */
UCLASS(Blueprintable, ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCommandProviderComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	UFUNCTION(BlueprintNativeEvent, Category = "Landship|Commands")
	TArray<FLSCommandEntry> GetCommands(ALSPlayerController* Controller) const;
	virtual TArray<FLSCommandEntry> GetCommands_Implementation(ALSPlayerController* Controller) const;
	/** 요청 시 비용·쿨타임·소유권을 다시 검사한다. UI는 자원을 차감하지 않는다. */
	UFUNCTION(BlueprintNativeEvent, Category = "Landship|Commands")
	bool ExecuteCommand(FName Id, ALSPlayerController* Controller);
	virtual bool ExecuteCommand_Implementation(FName Id, ALSPlayerController* Controller);
};
