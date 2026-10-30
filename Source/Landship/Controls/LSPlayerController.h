#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LSPlayerController.generated.h"

/** 선택과 명령 구현이 확장할 기반. 입력과 카메라는 해당 기능 작업에서 연결한다. */
UCLASS()
class LANDSHIP_API ALSPlayerController : public APlayerController
{
	GENERATED_BODY()
};
