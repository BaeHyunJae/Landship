#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Troops/LSTroopUnit.h"
#include "LSCastleEscortComponent.generated.h"

/** 성의 이동 방향과 병력별 추종 자리를 공유한다. 이동·공격 명령은 실행하지 않는다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSCastleEscortComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSCastleEscortComponent();
	/** 살아 있는 아군 병력에 빈 자리를 배정한다. 교전 중에도 자리를 유지하며 목적지는 성의 발밑 높이다. */
	bool TryGetDestination(ALSTroopUnit* Troop, FVector& OutDestination);
	float GetSlotSpacing() const { return SlotSpacing; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	    FActorComponentTickFunction* ThisTickFunction) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Escort")
	TArray<ELSTroopType> FrontlineTroopTypes = {ELSTroopType::Spearman, ELSTroopType::Cavalry,
	    ELSTroopType::Archer, ELSTroopType::Medic, ELSTroopType::Shieldbearer,
	    ELSTroopType::Mortar, ELSTroopType::Bomber};
	/** 성의 몸체에서 첫 추종 열까지의 간격(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Escort", meta = (ClampMin = "0", Units = "cm"))
	float CastleClearance = 350.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Escort", meta = (ClampMin = "1", Units = "cm"))
	float SlotSpacing = 220.0f;
	/** 중앙부터 좌우로 채울 한 열의 자리 수다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Escort", meta = (ClampMin = "1", ClampMax = "15"))
	int32 Columns = 5;
	/** 정지 부근 속도 잡음으로 편성 방향이 바뀌지 않게 한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Escort", meta = (ClampMin = "1", Units = "cm/s"))
	float DirectionSpeedThreshold = 25.0f;

private:
	void ReleaseInvalidSlots(TArray<TWeakObjectPtr<ALSTroopUnit>>& Slots, bool bFront);
	TArray<TWeakObjectPtr<ALSTroopUnit>> FrontSlots;
	TArray<TWeakObjectPtr<ALSTroopUnit>> RearSlots;
	FVector TravelDirection = FVector::ForwardVector;
};
