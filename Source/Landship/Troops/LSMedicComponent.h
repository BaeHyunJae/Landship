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
	virtual bool IsConfigurationValid() const override;
	virtual bool IsSupportBehavior() const override
	{
		return true;
	}
	virtual FLSCombatDirective EvaluateEngagement(AActor* Target, const TArray<ALSUnit*>& Allies) override;
	virtual void ResetForNewLife() override;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "0.01"))
	float HealingPerSecond = 24;
	UPROPERTY(EditDefaultsOnly, Category = "Medic", meta = (ClampMin = "1", Units = "cm"))
	float HealingRange = 500;
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
		return Patient.IsValid();
	}
	virtual FString GetRestingLabel() const override;

  private:
	bool IsPatientValid() const;
	void SetPatient(ALSTroopUnit* Unit);
	void HideBeam();
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
