#pragma once

#include "CoreMinimal.h"
#include "LSMeleeApproachPlanner.generated.h"

class ALSUnit;
class UNavigationSystemV1;

USTRUCT()
struct LANDSHIP_API FLSMeleeApproachSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "8", ClampMax = "32"))
	int32 SlotCount = 16;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxTargetCandidates = 4;
	/** 표적 하나를 검토할 때 경유지 검사를 포함한 동기 경로 질의 상한이다. */
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxPathChecks = 3;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "0.1", Units = "s"))
	float StuckSeconds = 1.2f;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "1", Units = "cm"))
	float ProgressDistance = 25;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "0", Units = "cm"))
	float Clearance = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "1", Units = "cm"))
	float TargetMoveDistance = 75;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "0.1", Units = "s"))
	float RetryInterval = 0.4f;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "0.1", Units = "s"))
	float BlockedSeconds = 2;
	UPROPERTY(EditDefaultsOnly, Category = "Approach", meta = (ClampMin = "1", Units = "cm"))
	float LocalLookAhead = 350;
	bool IsValid() const;
};

struct FLSMeleeApproachResult
{
	ALSUnit* Target = nullptr;
	FVector Destination = FVector::ZeroVector;
	bool bCanApproach = false;
};

/** 전투 모드가 소유하는 근접 접근 계획이다. 약한 참조만 보관하며 이동·공격·병종 스킬은 실행하지 않는다. */
class LANDSHIP_API FLSMeleeApproachPlanner
{
  public:
	FLSMeleeApproachResult Plan(ALSUnit* Unit, ALSUnit* PreferredTarget, const TArray<ALSUnit*>& Opponents, double Now,
	                            float AttackRange, const FLSMeleeApproachSettings& Settings);
	void Release(ALSUnit* Unit);
	void RejectDestination(ALSUnit* Unit, double Now, const FLSMeleeApproachSettings& Settings);
	void Prune();
	void Reset();

  private:
	struct FLSReservation
	{
		TWeakObjectPtr<ALSUnit> Target;
		FVector Goal = FVector::ZeroVector;
		FVector Destination = FVector::ZeroVector;
		FVector TargetLocation = FVector::ZeroVector;
		FVector BlockedPoint = FVector::ZeroVector;
		double LastProgressTime = 0;
		double NextPlanTime = 0;
		double AvoidPointUntil = 0;
		double BestDistance = 0;
		bool bReserved = false;
		bool bDetouring = false;
	};
	bool IsReserved(ALSUnit* Unit, const FVector& Goal, float Radius, float Clearance) const;
	bool FindGoal(ALSUnit* Unit, ALSUnit* Target, UNavigationSystemV1* Nav, float AttackRange,
	              const FLSMeleeApproachSettings& Settings, const FLSReservation& State, double Now, FVector& OutGoal,
	              FVector& OutDestination) const;
	TMap<TWeakObjectPtr<ALSUnit>, FLSReservation> Reservations;
};
