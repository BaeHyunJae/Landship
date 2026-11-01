#pragma once

#include "Controls/LSCommandProviderComponent.h"
#include "LSCastleCommandProviderComponent.generated.h"

/** 성의 무장·생산 기능을 공통 명령 카드에 연결한다. */
UCLASS()
class LANDSHIP_API ULSCastleCommandProviderComponent : public ULSCommandProviderComponent
{
	GENERATED_BODY()
  public:
	virtual TArray<FLSCommandEntry> GetCommands_Implementation(ALSPlayerController* Controller) const override;
	virtual bool ExecuteCommand_Implementation(FName Id, ALSPlayerController* Controller) override;
};
