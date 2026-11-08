#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/LSPhysicalBlocker.h"
#include "LSShellNavigationBodyComponent.generated.h"

class UPrimitiveComponent;

/** 개선 실물 하나의 정착·이동에 따라 실제 primitive의 내비게이션 등록을 전환한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSShellNavigationBodyComponent : public UActorComponent, public ILSPhysicalBlocker
{
	GENERATED_BODY()
public:
	ULSShellNavigationBodyComponent();
	/** 몸체 등록 전에 호출한다. 기존 몸체 등록은 해제하고 새 몸체를 처음에는 nav에서 제외한다. */
	void SetNavigationBody(UPrimitiveComponent* InBody);
	/** 유효한 개선 전장과 몸체 준비가 끝난 뒤 켠다. 끄면 물리 차단 참여와 nav 등록을 함께 해제한다. */
	void SetTrackingEnabled(bool bEnabled);
	UPrimitiveComponent* GetNavigationBody() const { return Body.Get(); }
	virtual UPrimitiveComponent* GetPhysicalBlockingBody() const override;
	bool IsNavigationRegistered() const { return bNavigationRegistered; }
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Navigation", meta = (ClampMin = "0", Units = "cm/s"))
	float SettleLinearSpeed = 20;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Navigation", meta = (ClampMin = "0", Units = "deg/s"))
	float SettleAngularSpeed = 5;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Navigation", meta = (ClampMin = "0", Units = "s"))
	float SettleSeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Navigation", meta = (ClampMin = "0", Units = "cm"))
	float SettleDisplacement = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Navigation", meta = (ClampMin = "0", Units = "cm"))
	float WakeMovementDistance = 10;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") uint32 NavigationRevision = 0;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") int32 LastDirtyAreasBefore = -1;
	UPROPERTY(VisibleInstanceOnly, Category = "Shell Experiment|Observation") int32 LastDirtyAreasAfter = -1;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	void SetNavigationRegistered(bool bRelevant);
	void BindWakeEvent();
	UFUNCTION() void OnBodyWake(UPrimitiveComponent* WakingComponent, FName BoneName);
	TWeakObjectPtr<UPrimitiveComponent> Body;
	FTransform NavigationReferenceTransform;
	FVector SettleWindowOrigin = FVector::ZeroVector;
	float StableSeconds = 0;
	bool bTrackingEnabled = false;
	bool bNavigationRegistered = false;
	bool bWasAwake = false;
};
