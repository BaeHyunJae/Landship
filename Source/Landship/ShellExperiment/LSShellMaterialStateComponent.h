#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "LSShellMaterialStateComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UPhysicalMaterial;
class UPhysicsConstraintComponent;
class ULSEnemyBehaviorComponent;
class ULSBasicAttackComponent;
class ALSShellTransport;
class ULSShellSiegeController;

struct LANDSHIP_API FLSShellMaterialReplacement
{
	TWeakObjectPtr<ULSShellReactiveComponent> PreviousReactive;
	TWeakObjectPtr<ULSShellReactiveComponent> ReplacementReactive;
	TWeakObjectPtr<ALSShellInertIce> Ice;
	FLSShellRigidBodyState CapturedState;
	bool IsValid() const { return PreviousReactive.IsValid() && ReplacementReactive.IsValid() && Ice.IsValid(); }
};

struct FLSShellAdhesiveBond
{
	TWeakObjectPtr<class ULSShellMaterialStateComponent> Other;
	TWeakObjectPtr<UPhysicsConstraintComponent> Constraint;
	TWeakObjectPtr<UStaticMeshComponent> LinkVisual;
	TWeakObjectPtr<UStaticMeshComponent> BandVisual;
	FVector LocalAnchor = FVector::ZeroVector;
	FVector OtherLocalAnchor = FVector::ZeroVector;
	FVector LocalContactNormal = FVector::UpVector;
	float MaximumJointImpulse = 0;
};

struct FLSShellMaterialBondTransfer
{
	TWeakObjectPtr<ULSShellMaterialStateComponent> Other;
	FVector ContactPoint = FVector::ZeroVector;
	float MaximumJointImpulse = 0;
};

UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSShellMaterialStateComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSShellMaterialStateComponent();
	static ULSShellMaterialStateComponent* GetOrCreate(AActor* Actor);
	/** 실제 몸체 하나만 무장한다. 슬롯·강도는 이미 분배한 예산이며 실패하거나 끊어진 슬롯은 복구하지 않는다. */
	int32 ApplyAdhesion(int32 BondSlots, float StrengthScale = 1);
	/** 살아 있는 대상은 남은 시간과 입력의 최댓값을 쓴다. 기존 체력·질량·전하는 바꾸지 않는다. */
	bool ApplyCooling(float DurationSeconds = 4);
	UFUNCTION(BlueprintPure) bool IsCooled() const;
	UFUNCTION(BlueprintPure) bool IsAdhesive() const { return !AvailableBondStrengths.IsEmpty() || GetActiveBondCount() > 0; }
	UFUNCTION(BlueprintPure) int32 GetActiveBondCount() const;
	UFUNCTION(BlueprintPure) int32 GetAvailableBondSlots() const { return AvailableBondStrengths.Num(); }
	UFUNCTION(BlueprintPure) float GetColdSecondsRemaining() const;
	FString GetRecoveryDiagnostics() const;
	float GetKineticDamageMultiplier() const { return IsCooled() ? 2 : 1; }
	bool ShouldShatter(float NormalSpeed) const;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Cooling") float ShatterNormalSpeed = 900;
	/** 인장·압축을 포함한 한 물리 스텝의 선형 충격 상한(kg·cm/s)이다. PostPhysics에서 비교한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Adhesion") float BondLinearBreakImpulse = 450000;
	float GetPeakBondImpulse() const { return PeakBondImpulse; }
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float RightingAngularGain = 40;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float RightingDamping = 12;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float MaximumRightingAngularAcceleration = 60;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float ResumeMaximumSpeed = 30;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float ResumeMaximumTiltDegrees = 10;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Recovery") float ResumeStableSeconds = .5f;
	FLSShellRigidBodyState CaptureBodyState() const;
	/** 제거 정책이 솔버 몸체를 끄기 전에 현재 질량·형상·풀이 뒤 속도와 미소비 충격을 보관한다. */
	void CaptureBeforePhysicalDeath();
	/** root가 사망 전에 캡처한 상태를 넘긴다. 전하·식별값·예약 효과 이동은 하지 않는다. */
	FLSShellMaterialReplacement CreateIceReplacement(const FLSShellRigidBodyState& CapturedState);
	/** root의 식별값·전하·예약 효과 교체 뒤, 원본 EndPlay 전에 한 번 호출한다. 실제로 닿는 결속만 다시 연결한다. */
	void CommitMaterialTransfer(ULSShellMaterialStateComponent* Replacement);
	/** 자식 3개를 바꾸지 않고 준비 상태만 검사한다. 성공 뒤 몸체를 바꾸지 않은 채 Commit을 호출한다. */
	bool PrepareSplitTransfer(const TArray<AActor*>& Children, FString& Error) const;
	/** 남은 냉각·미사용 슬롯을 나누고, 실현 결속은 접촉 영역을 보존하는 자식 하나에만 옮긴다. */
	void CommitSplitTransfer(const TArray<AActor*>& Children);
	void SetPermanentIce();
	/** 종료·회수 시 모든 실제 결속을 해제한다. 소비한 접착 예산은 반환하지 않는다. */
	void ReleaseAllBonds();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	ULSShellReactiveComponent* GetReactive() const;
	FLSShellRigidBodyState ReadCurrentBodyState() const;
	bool EnsureRigidCharacter();
	bool HasRigidControlReason() const;
	bool IsGrounded() const;
	void SetControlReason(FName Reason, bool bHeld);
	void UpdateColdMaterial(bool bCold);
	void UpdatePresentation();
	void UpdateBondPresentation(FLSShellAdhesiveBond& Bond);
	void RefreshBonds(bool bMayCreateNew = true);
	bool CreateBond(ULSShellMaterialStateComponent* Other, float MaximumImpulse, FVector ContactPoint);
	void RemoveOwnedBond(int32 Index);
	void ReleaseBondWith(ULSShellMaterialStateComponent* Other);
	TArray<FLSShellMaterialBondTransfer> CaptureBondTransfers() const;
	bool AdoptBondTransfer(const FLSShellMaterialBondTransfer& Transfer);
	void TryResumeCharacter(float DeltaSeconds);
	UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> ColdMaterial;
	UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> PreviousMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> FrostVisuals;
	TWeakObjectPtr<UPrimitiveComponent> FrostBody;
	FLSShellRigidBodyState PreparedDeathState;
	FLSShellRigidBodyState RecentPostPhysicsState;
	uint64 SnapshotSequence = 0;
	uint64 PreparedDeathSequence = 0;
	uint64 RecentPostPhysicsSequence = 0;
	TArray<FLSShellAdhesiveBond> Bonds;
	TSet<TWeakObjectPtr<ULSShellMaterialStateComponent>> BondOwners;
	TArray<float> AvailableBondStrengths;
	TMap<FName, TArray<TWeakObjectPtr<ALSShellTransport>>> ControlOwners;
	TMap<FName, TArray<TWeakObjectPtr<ULSShellSiegeController>>> SiegeControlOwners;
	TWeakObjectPtr<ACharacter> Character;
	TWeakObjectPtr<ULSEnemyBehaviorComponent> Behavior;
	TWeakObjectPtr<ULSBasicAttackComponent> Attack;
	double ColdUntil = 0;
	float StableSeconds = 0;
	float PeakBondImpulse = 0;
	bool bPermanentIce = false;
	bool bRigidCharacter = false;
	bool bBehaviorWasTicking = false;
	bool bAttackWasTicking = false;
	bool bMovementWasTicking = false;
	bool bMovementAvoidance = false;
	bool bColdMaterialApplied = false;
	bool bTransferCommitted = false;
	uint8 PreviousMovementMode = 0;
	ECollisionEnabled::Type PreviousCollisionMode = ECollisionEnabled::QueryOnly;
	uint8 PreviousCustomMovementMode = 0;
};
