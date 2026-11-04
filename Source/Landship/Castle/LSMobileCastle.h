#pragma once

#include "Troops/LSUnit.h"
#include "LSMobileCastle.generated.h"

/** 테스트 전용 성 클래스다. 실제 게임의 성 기능은 ALSCastle에 구현하며, 이 클래스에 기능을 추가하지 않는다. */
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
