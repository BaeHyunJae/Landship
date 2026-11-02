#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthTypes.h"
#include "LSEnemyCombatProbe.generated.h"

class ALSMobileCastle;
class ALSEnemyOutpost;
class ALSEnemyTroop;
class ALSBasicAttackTestActor;
class ALSTroopUnit;
class ULSCastleWeaponData;
struct FLSOutpostCaptureSnapshot;

DECLARE_MULTICAST_DELEGATE_TwoParams(FLSBeforePublicAttackOrder, AActor*, AActor*);

/** 전용 실행 인자가 있을 때만 실제 맵의 이동과 공격을 관찰한다. */
UCLASS()
class LANDSHIP_API ALSEnemyCombatProbe : public AActor
{
	GENERATED_BODY()
public:
	ALSEnemyCombatProbe();
	FLSBeforePublicAttackOrder OnBeforePublicAttackOrderEvent;
protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	void Check(bool bPassed, const TCHAR* Message);
	void Finish();
	void BeginRecoveryCase(bool bShortRoute);
	void CaptureCore();
	UFUNCTION()
	void OnExecuted(const FLSAttackExecution& Execution);
	UFUNCTION()
	void OnCastleDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnInvestigationDamaged(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnWeaponCoreCaptured(const FLSOutpostCaptureSnapshot& Capture);
	UFUNCTION()
	void OnWeaponOutpostCaptured(const FLSOutpostCaptureSnapshot& Capture);
	UFUNCTION()
	void OnWeaponCoreDied(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnPublicCoreDamaged(const FLSDamageResult& Damage);
	TWeakObjectPtr<ALSMobileCastle> Castle;
	TWeakObjectPtr<ALSEnemyOutpost> Core;
	TArray<TWeakObjectPtr<ALSEnemyTroop>> Units;
	TMap<TWeakObjectPtr<ALSEnemyTroop>, FVector> Starts;
	TSet<TWeakObjectPtr<AActor>> ExecutedSources;
	TSet<TWeakObjectPtr<AActor>> DamageSources;
	TWeakObjectPtr<ALSBasicAttackTestActor> SupportAttacker;
	TWeakObjectPtr<ALSTroopUnit> ReturnBlocker;
	bool bObservedReturnBlocked = false;
	bool bRemovedReturnBlocker = false;
	TWeakObjectPtr<ALSEnemyTroop> SupportMelee;
	TWeakObjectPtr<ALSEnemyTroop> SupportRanged;
	TWeakObjectPtr<ALSBasicAttackTestActor> InvestigationTarget;
	TWeakObjectPtr<ALSBasicAttackTestActor> RecoveryDecoy;
	TWeakObjectPtr<ALSEnemyTroop> RecoveryUnit;
	FVector RecoveryCenter = FVector::ZeroVector;
	float InvestigationHealth = 0;
	bool bInvestigationDamagedByGuard = false;
	bool bInvestigationUsedArrivalScan = false;
	float ProgressBeforeGoalChange = 0;
	float MaximumRecoveryProgress = 0;
	bool bObservedRecovery = false;
	bool bChangedRecoveryGoal = false;
	bool bRecoveryContractHeld = true;
	FVector SupportMeleeStart = FVector::ZeroVector;
	FVector SupportRangedStart = FVector::ZeroVector;
	FVector ReportedAttackPosition = FVector::ZeroVector;
	float CoreHealthBeforeSupport = 0;
	float SupportMeleeMovement = 0;
	float SupportRangedMovement = 0;
	float ClosestReportedPosition = TNumericLimits<float>::Max();
	bool bObservedSupporting = false;
	float CoreHealthBeforePublicAttack = 0;
	TWeakObjectPtr<ALSTroopUnit> PublicAttackTroop;
	TWeakObjectPtr<ALSEnemyTroop> MovingSoloUnit;
	TSet<TWeakObjectPtr<AActor>> MovingDamageSources;
	int32 Stage = 0;
	int32 Failures = 0;
	float StageTime = 0;
	float TotalTime = 0;
	float MaximumGuardDistance = 0;
	int32 WeaponCoreCaptures = 0;
	int32 WeaponOutpostCaptures = 0;
	/** 무장 함락 검사에서 성에 설치한 대포 정의 사본. 공유 DA_Cannon 대신 이 사본의 피해를 바꾼다. */
	UPROPERTY(Transient)
	TObjectPtr<ULSCastleWeaponData> WeaponCaptureCannon;
	bool bWeaponLethalSourceObserved = false;
	bool bWeaponCaptureCleanupObserved = true;
	bool bPublicDamageSourceObserved = false;
};
