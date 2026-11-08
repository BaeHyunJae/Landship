#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSFactionInterface.h"
#include "LSShellWeakPoint.generated.h"

class ALSShellExperimentEnemy;
class USphereComponent;
class ULSHealthComponent;
class ULSVisionParticipantComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FLSDamageResult;

UCLASS(Blueprintable, NotPlaceable)
class LANDSHIP_API ALSShellWeakPoint : public AActor, public ILSFactionInterface
{
	GENERATED_BODY()
public:
	ALSShellWeakPoint();
	/** 지연 생성의 FinishSpawning 전에 같은 월드의 Heavy를 한 번 지정한다. */
	bool SetInitialHeavy(ALSShellExperimentEnemy* InHeavy);
	ALSShellExperimentEnemy* GetHeavyParent() const;
	ULSHealthComponent* GetHealthComponent() const { return Health; }
	virtual ELSFaction GetFaction() const override { return ELSFaction::Enemy; }
	/** 본체 사망·제거의 정리다. 체력을 깎거나 기폭 알림을 발생시키지 않는다. */
	void RemoveWithoutDetonation();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<USphereComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<ULSHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<ULSVisionParticipantComponent> Vision;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell Experiment") TObjectPtr<UTextRenderComponent> CoreLabel;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Shell Experiment", meta = (ClampMin = "0.01")) float MaxHealth = 200;
protected:
	virtual void BeginPlay() override;
private:
	void PrepareForDeath();
	UFUNCTION() void OnDamaged(const FLSDamageResult& Damage);
	UFUNCTION() void OnDied(const FLSDamageResult& Damage);
	TWeakObjectPtr<ALSShellExperimentEnemy> HeavyParent;
	bool bRemovedWithoutDetonation = false;
};
