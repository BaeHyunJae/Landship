#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSBattlefieldSetup.generated.h"

class ALSOutpost;

/** 맵에서 플레이어 성과 핵심 거점을 명시적으로 연결한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSBattlefieldSetup : public AActor
{
	GENERATED_BODY()
public:
	ALSBattlefieldSetup();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Landship|Battlefield")
	TObjectPtr<AActor> PlayerCastle;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Landship|Battlefield")
	TObjectPtr<ALSOutpost> CoreOutpost;

	FString GetConfigurationError() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
