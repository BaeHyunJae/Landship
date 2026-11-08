#pragma once

#include "CoreMinimal.h"
#include "Match/LSGameMode.h"
#include "LSShellExperimentGameMode.generated.h"

UCLASS()
class LANDSHIP_API ALSShellExperimentGameMode : public ALSGameMode
{
	GENERATED_BODY()
public:
	ALSShellExperimentGameMode();
	virtual void StartPlay() override;
	UFUNCTION(BlueprintPure) bool IsConfigurationValid() const { return bConfigurationValid; }
	UFUNCTION(BlueprintPure) FString GetConfigurationError() const { return ConfigurationError; }
private:
	bool bConfigurationValid = false;
	FString ConfigurationError;
};
