#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LSGameMode.generated.h"

/** 전투 기능들이 연결할 공통 게임 모드. 승패 판정은 별도 작업에서 추가한다. */
UCLASS()
class LANDSHIP_API ALSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALSGameMode();
	virtual void InitGameState() override;
};
