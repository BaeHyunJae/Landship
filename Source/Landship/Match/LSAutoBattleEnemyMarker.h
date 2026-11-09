#pragma once

#include "GameFramework/Actor.h"
#include "Troops/LSTroopUnit.h"
#include "LSAutoBattleEnemyMarker.generated.h"

class ALSAutoBattleScenario;
class UStaticMeshComponent;
class UTextRenderComponent;

/** 에디터의 이동·Alt 복제·Delete로 편집한다. 전투 액터가 아니며 쿠킹에서 제외된다. */
UCLASS(NotBlueprintable)
class LANDSHIP_API ALSAutoBattleEnemyMarker : public AActor
{
	GENERATED_BODY()
  public:
	ALSAutoBattleEnemyMarker();
	virtual bool IsEditorOnly() const override
	{
		return true;
	}
	virtual void OnConstruction(const FTransform& Transform) override;
	UPROPERTY(EditAnywhere, Category = "Enemy Placement")
	ELSTroopType TroopType = ELSTroopType::Spearman;
	/** 동일 레벨의 시나리오 하나에 속한다. Alt 복제 시 이 연결을 유지한다. */
	UPROPERTY(VisibleInstanceOnly, Category = "Enemy Placement")
	TObjectPtr<ALSAutoBattleScenario> Scenario;

  protected:
	virtual void BeginPlay() override;

  private:
	UPROPERTY(VisibleAnywhere, Category = "Enemy Placement")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "Enemy Placement")
	TObjectPtr<UTextRenderComponent> Label;
};
