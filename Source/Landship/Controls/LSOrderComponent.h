#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSOrderComponent.generated.h"

class ULSBasicAttackComponent;

UENUM(BlueprintType)
enum class ELSOrderKind : uint8 { Idle, Move, AttackTarget, AttackMove, Hold };

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSOrder
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite)
	ELSOrderKind Kind = ELSOrderKind::Idle;
	UPROPERTY(BlueprintReadWrite)
	FVector Destination = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite)
	TWeakObjectPtr<AActor> Target;
};

/** 이동·교전을 한 명령 상태에서 제어해 일반 이동과 공격 이동을 구분한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSOrderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSOrderComponent();
	UFUNCTION(BlueprintCallable, Category = "Landship|Orders")
	bool SubmitOrder(const FLSOrder& Order, bool bQueue = false);
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	ELSOrderKind GetOrderKind() const { return ActiveOrder.Kind; }
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	int32 GetQueuedCount() const { return PendingOrders.Num(); }
	UFUNCTION(BlueprintPure, Category = "Landship|Orders")
	AActor* GetEngagedTarget() const { return EngagedTarget.Get(); }
	bool CanAttack() const;
	bool CanMove() const;
	static TArray<FVector> MakeFormation(const FVector& Center, int32 Count, float Spacing);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "1"))
	int32 MaxQueuedOrders = 64;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0.05"))
	float RepathInterval = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Orders", meta = (ClampMin = "0"))
	float ArrivalTolerance = 30.0f;
private:
	bool ValidateOrder(const FLSOrder& Order) const;
	bool CanReach(const FVector& Goal) const;
	bool MoveTo(const FVector& Goal);
	void StopMovement();
	bool IsMoving() const;
	bool IsHostile(AActor* Target) const;
	bool IsInAttackRange(AActor* Target) const;
	float GetDetectionRange() const;
	AActor* FindEnemy(float Range) const;
	void BeginOrder(const FLSOrder& Order);
	void FinishOrder();
	void Engage(AActor* Target, bool bAllowChase);
	UPROPERTY(Transient)
	FLSOrder ActiveOrder;
	UPROPERTY(Transient)
	TArray<FLSOrder> PendingOrders;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> EngagedTarget;
	UPROPERTY(Transient)
	TObjectPtr<ULSBasicAttackComponent> Attack;
	double NextRepathTime = 0;
	bool bHasEngagement = false;
};
