#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSVisionPresentation.generated.h"

class ALSBattlefieldSettings;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UPostProcessComponent;

/** 전장 좌표로 지형 배경과 안개를 그린다. 가시성 판정은 시야 서브시스템의 확정 결과만 사용한다. */
UCLASS(BlueprintType)
class LANDSHIP_API ALSVisionPresentation : public AActor
{
	GENERATED_BODY()
  public:
	ALSVisionPresentation();
	static ALSVisionPresentation* Find(const UWorld* World);
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	UTextureRenderTarget2D* GetTerrainTexture() const
	{
		return TerrainTexture;
	}
	/** RGB는 검정이며 알파는 시야 밖 어둡기다. 월드와 지도에서 같은 텍스처를 사용한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	UTextureRenderTarget2D* GetVisibilityTexture() const
	{
		return VisibilityTexture;
	}
	UFUNCTION(BlueprintCallable, Category = "Landship|Vision")
	void CaptureTerrain();

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
	UFUNCTION()
	void UpdateVisibility();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> TerrainCapture;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPostProcessComponent> FogPostProcess;
	UPROPERTY(Transient)
	TObjectPtr<ALSBattlefieldSettings> Settings;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TerrainTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> VisibilityTexture;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogInstance;
	int32 TextureWidth = 512;
	float OutsideDarkness = 0.72f;
};
