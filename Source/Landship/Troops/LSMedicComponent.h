#pragma once

#include "Troops/LSTroopCombatComponent.h"
#include "LSMedicComponent.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** 아군 한 명의 생명을 고정해 치유한다. 이동은 공통 교전 판단 결과로만 요청한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSMedicComponent : public ULSTroopCombatComponent
{
	GENERATED_BODY()
  public:
	ULSMedicComponent();
	virtual float GetEngagementSearchRadius() const override { return PatientDetectionRange; }
	virtual bool IsConfigurationValid() const override;
	virtual bool IsSupportBehavior() const override
	{
		return true;
	}
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Allies) override;
	virtual void ResetForNewLife() override;
	UFUNCTION(BlueprintPure, Category = "Medic|Mana")
	float GetCurrentMana() const
	{
		return CurrentMana;
	}
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Medic|Mana", meta = (ClampMin = "0.01"))
	float MaxMana = 100;
	UPROPERTY(EditDefaultsOnly, Category = "Medic|Mana", meta = (ClampMin = "0"))
	float StartingMana = 50;
	UPROPERTY(EditDefaultsOnly, Category = "Medic|Mana", meta = (ClampMin = "0"))
	float ManaRegenerationPerSecond = 1;
	/** 실제 회복한 HP당 소모량이다. 완치된 대상이나 실패한 회복에는 소모하지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Medic|Mana", meta = (ClampMin = "0.001"))
	float ManaPerHealth = 0.5f;
	/** 마나 고갈 뒤 매 프레임 연결/해제가 반복되지 않도록 새 연결에 필요한 최소량이다. */
	UPROPERTY(EditDefaultsOnly, Category = "Medic|Mana", meta = (ClampMin = "0.01"))
	float ManaToStartHealing = 5;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "0.01"))
	float HealingPerSecond = 24;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "1", Units = "cm"))
	float HealingRange = 500;
	/** 새 부상자를 찾는 거리(cm). 이미 선택한 환자는 병종 데이터의 감지 거리까지 추적한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "1", Units = "cm"))
	float PatientDetectionRange = 1200;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "0.01", Units = "s"))
	float HealingInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "0.1", Units = "cm"))
	float BeamRadius = 4;

  protected:
	virtual void BeginPlay() override;
	virtual void CancelSpecialAction() override;
	virtual void TickSpecialAction(float DeltaTime) override;
	virtual bool HasSpecialAction() const override
	{
		return Patient.IsValid() || (ManaRegenerationPerSecond > 0 && CurrentMana < MaxMana);
	}
	virtual FString GetRestingLabel() const override;

  private:
	bool IsPatientValid() const;
	void SetPatient(ALSTroopUnit* Unit);
	void HideBeam();
	void ReleasePatient();
	void ReleaseHealingConnection();
	void UpdateManaLabel();
	bool CanConnectHealing() const;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Medic|Mana",
	          meta = (AllowPrivateAccess = "true"))
	float CurrentMana = 0;
	uint64 HealingClaimSerial = 0;
	uint64 ManaLifeSerial = 0;
	float LastDisplayedMana = -1;
	bool bLastDisplayedBeam = false;
	UPROPERTY(Transient)
	TWeakObjectPtr<ALSTroopUnit> Patient;
	uint64 PatientSerial = 0;
	bool bPooledPatient = false;
	float AccumulatedHealingTime = 0;
	bool bBeamVisible = false;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY()
	TObjectPtr<UStaticMesh> BeamMesh;
	UPROPERTY(EditDefaultsOnly, Category = "Medic")
	TSoftObjectPtr<UMaterialInterface> BeamMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterialInstance;
};
