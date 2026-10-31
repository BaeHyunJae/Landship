#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSBattlefieldSettings.generated.h"

/** 지도와 시야가 공유하는 전장 경계와 명시적인 지형 목록이다. 액터 변환은 경계에 영향을 주지 않는다. */
UCLASS(Blueprintable)
class LANDSHIP_API ALSBattlefieldSettings : public AActor
{
	GENERATED_BODY()

  public:
	ALSBattlefieldSettings();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Battlefield")
	FVector2D BoundsMin = FVector2D(-2400.0, -2000.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Battlefield")
	FVector2D BoundsMax = FVector2D(2400.0, 2000.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Battlefield")
	float GroundHeight = 0.0f;

	/** 지도에 지형으로 표시할 액터만 직접 지정한다. 전투 대상은 넣지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Battlefield")
	TArray<TObjectPtr<AActor>> TerrainActors;

	/** 현재 시야 마스크를 읽어 지형을 어둡게 하는 후처리 머티리얼이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Battlefield")
	TObjectPtr<class UMaterialInterface> FogMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Vision",
			  meta = (ClampMin = "0.01", Units = "s"))
	float VisionRefreshInterval = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Vision",
			  meta = (ClampMin = "64", ClampMax = "2048"))
	int32 VisibilityTextureWidth = 512;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Vision",
			  meta = (ClampMin = "0", ClampMax = "1"))
	float OutsideDarkness = 0.72f;

	FBox2D GetWorldBounds() const
	{
		return FBox2D(BoundsMin, BoundsMax);
	}
	float GetGroundHeight() const
	{
		return GroundHeight;
	}
	const TArray<TObjectPtr<AActor>>& GetTerrainActors() const
	{
		return TerrainActors;
	}
	bool HasValidSettings() const;
	TArray<AActor*> FindUnconfiguredParticipants() const;

	/** 유효한 설정이 정확히 하나일 때만 반환한다. 지형이나 경계를 추측하지 않는다. */
	static ALSBattlefieldSettings* Find(const UWorld* World);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void PostRegisterAllComponents() override;
	virtual void OnConstruction(const FTransform& Transform) override;
#endif

  private:
#if WITH_EDITOR
	void UpdateBoundsPreview();
#endif
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<class UBoxComponent> BoundsPreview;
#endif
};
