#pragma once

#include "Troops/LSUnit.h"
#include "LSMobileCastle.generated.h"

/** 병력의 이동·명령·전투를 재사용하는 대형 성 프로토타입. 시설과 무장은 별도 단계다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSMobileCastle : public ALSUnit
{
	GENERATED_BODY()

  public:
	ALSMobileCastle();
	virtual bool SupportsBasicAttack() const override
	{
		return false;
	}
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Weapons")
	TObjectPtr<class ULSCastleWeaponComponent> Weapons;

  protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Castle")
	TObjectPtr<class UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Castle")
	TObjectPtr<class UStaticMeshComponent> LeftTower;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landship|Castle")
	TObjectPtr<class UStaticMeshComponent> RightTower;

  private:
	void UpdateTeamColor();
};
