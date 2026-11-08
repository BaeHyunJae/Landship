#pragma once

#include "CoreMinimal.h"
#include "Obstacles/LSPathObstacle.h"
#include "LSShellExplosive.generated.h"

class UBoxComponent;
class ULSShellReactiveComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellExplosive : public ALSPathObstacle
{
	GENERATED_BODY()
public:
	ALSShellExplosive();
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment") bool bAuxiliaryFacility = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UBoxComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<ULSShellReactiveComponent> Reactive;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UStaticMeshComponent> Footing;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UTextRenderComponent> RoleLabel;
	UFUNCTION(BlueprintCallable) bool RequestDetonation();
protected:
	virtual void BeginPlay() override;
private:
	void RefreshRoleVisual();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> UpperFrame;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LowerFrame;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RoleMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BaseMaterial;
};
