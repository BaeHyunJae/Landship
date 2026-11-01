#pragma once

#include "Controls/LSCommandProviderComponent.h"
#include "LSCommandTestProvider.generated.h"

/** 자동화에서만 사용하는 확장 계약 검증용 제공자. 게임 유닛에는 부착하지 않는다. */
UCLASS(Transient, NotBlueprintable)
class ULSCommandTestProvider : public ULSCommandProviderComponent
{
	GENERATED_BODY()
  public:
	TArray<FLSCommandEntry> Entries;
	int32 Executions = 0;
	virtual TArray<FLSCommandEntry> GetCommands_Implementation(ALSPlayerController*) const override
	{
		return Entries;
	}
	virtual bool ExecuteCommand_Implementation(FName Id, ALSPlayerController*) override
	{
		for (const auto& Entry : Entries)
			if (Entry.Id == Id && Entry.bEnabled)
			{
				++Executions;
				return true;
			}
		return false;
	}
};
