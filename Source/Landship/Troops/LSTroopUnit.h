#pragma once

#include "Troops/LSUnit.h"
#include "Combat/LSBasicAttackComponent.h"
#include "LSTroopUnit.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
struct FLSDamageResult;

UENUM(BlueprintType)
enum class ELSTroopType : uint8
{
	Spearman,
	Archer,
	Cavalry
};

/** 공통 전투·이동을 재사용하고 병종별 데이터 행과 임시 외형만 선택한다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSTroopUnit : public ALSUnit
{
	GENERATED_BODY()

  public:
	ALSTroopUnit();
	virtual void OnConstruction(const FTransform& Transform) override;
	UFUNCTION(BlueprintPure, Category = "Landship|Troops")
	ELSTroopType GetTroopType() const
	{
		return TroopType;
	}
	/** 생성 완료·전투 준비 전에 지정한다. 전투 중 변경과 잘못된 병종은 거부한다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Troops")
	bool SetInitialTroopType(ELSTroopType InType);
	UStaticMeshComponent* GetTroopMarker() const
	{
		return TroopMarker;
	}
	static FName GetStatsRowName(ELSTroopType Type);

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* Property) const override;
#endif

  protected:
	virtual void BeginPlay() override;

  private:
	void RefreshDefinition();
	UFUNCTION()
	void OnTroopDied(const FLSDamageResult& Damage);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Troops",
	          meta = (ExposeOnSpawn = "true", AllowPrivateAccess = "true"))
	ELSTroopType TroopType = ELSTroopType::Spearman;
	/** 병종 수치는 이 테이블에서 조정한다. 초기화 실패는 부모의 공통 규칙대로 차단한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	TSoftObjectPtr<UDataTable> TroopStatsTable;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	TSoftObjectPtr<UMaterialInterface> PrototypeMaterial;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Troops")
	TObjectPtr<UStaticMeshComponent> TroopMarker;
	UPROPERTY(VisibleAnywhere, Category = "Landship|Troops")
	TObjectPtr<UTextRenderComponent> TroopLabel;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SpearShape;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ArcherShape;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CavalryShape;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Troops")
	FLSProjectileAttackSettings RangedAttack;
	bool bTroopTypeLocked = false;
};
