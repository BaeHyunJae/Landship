#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSShellLauncherVisual.generated.h"

UCLASS(ClassGroup = (Landship))
class LANDSHIP_API ULSShellLauncherVisualComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSShellLauncherVisualComponent();
	/** 소유 액터의 루트 아래에 외형만 만든다. 성의 기존 컴포넌트와 충돌은 바꾸지 않는다. */
	void Initialize(class ALSCastle* InCastle);
	/** 호출자가 확정한 월드 좌표를 쓴다. 포신 끝은 Muzzle에 놓인다. */
	void SetPose(FVector Origin, FVector Muzzle, FVector Direction);
	void Fire(float Strength = 1);
	void Hide();
protected:
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	void UpdateMeshes();
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Base;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Barrel;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> MuzzleRim;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> MuzzleFlash;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FlashMaterial;
	TWeakObjectPtr<class ALSCastle> Castle;
	FVector PoseOrigin = FVector::ZeroVector;
	FVector PoseMuzzle = FVector::ZeroVector;
	FVector PoseDirection = FVector::ForwardVector;
	float RecoilAge = 0.25f;
	float RecoilScale = 1;
};
