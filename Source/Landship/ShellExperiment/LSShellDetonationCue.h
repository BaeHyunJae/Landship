#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSShellExperimentTypes.h"
#include "LSShellDetonationCue.generated.h"

UCLASS()
class LANDSHIP_API ALSShellDetonationCue : public AActor
{
	GENERATED_BODY()
public:
	ALSShellDetonationCue();
	/** 확정된 판정의 값만 복사한다. 지연 재생은 게임 판정이나 원본 액터에 의존하지 않는다. */
	void Initialize(const FLSShellCueSpec& Spec);
	void InitializeStoredCharge(class ULSShellReactiveComponent* Reactive, UPrimitiveComponent* Body);
	virtual void Tick(float DeltaSeconds) override;
private:
	void ActivateBurst();
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Ignition;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Flash;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Shockwave;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Link;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> ThrustHead;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> CapsuleEndA;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> CapsuleEndB;
	UPROPERTY() TObjectPtr<class UTextRenderComponent> Caption;
	UPROPERTY() TObjectPtr<class UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FlashMaterial;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> RingMaterial;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> LinkMaterial;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> HeadMaterial;
	UPROPERTY() TObjectPtr<class USoundBase> Sound;
	float Age = 0;
	float Delay = 0;
	float Radius = 900;
	float Strength = 1;
	ELSShellCueStyle Style = ELSShellCueStyle::Legacy;
	FVector Direction = FVector::ForwardVector;
	float DurationSeconds = 0.8f;
	float FlashSeconds = 0.18f;
	float PitchMultiplier = 1;
	float CaptionHeight = 160;
	TWeakObjectPtr<class ULSShellReactiveComponent> ChargeReactive;
	TWeakObjectPtr<UPrimitiveComponent> ChargeBody;
	uint32 ChargeGeneration = 0;
	bool bStoredCharge = false;
	bool bSurfaceCharge = false;
	bool bCapsuleShell = false;
	bool bActive = false;
};
