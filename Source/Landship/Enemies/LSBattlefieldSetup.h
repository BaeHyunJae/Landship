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

	/** 실제 게임 성 ALSCastle 또는 그 파생만 허용한다. 병력 이동 시험용 MobileCastle은 지정할 수 없다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Landship|Battlefield", meta = (AllowedClasses = "/Script/Landship.LSCastle"))
	TObjectPtr<AActor> PlayerCastle;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Landship|Battlefield")
	TObjectPtr<ALSOutpost> CoreOutpost;

	/** 아군 병력은 직접 명령 대신 시야 내 교전과 성 추종을 사용한다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Landship|Battlefield")
	bool bAutomaticAlliedTroops = true;

	FString GetConfigurationError() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
