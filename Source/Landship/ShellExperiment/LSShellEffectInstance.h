#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSShellExperimentTypes.h"
#include "LSShellEffectInstance.generated.h"

UCLASS()
class LANDSHIP_API ALSShellEffectInstance : public AActor
{
	GENERATED_BODY()
public:
	ALSShellEffectInstance();
	void Initialize(class ALSShellExperimentCoordinator* InCoordinator, const FLSShellShot& InShot, const FHitResult& Hit, bool bInCombatImpact = false, AActor* BodyTarget = nullptr);
	void RemapAnchor(AActor* Previous, AActor* Replacement);
	void RetargetGeneratedBody(AActor* Generated);
	void CancelGeneratedFollowup() { bGeneratedBodyFailed = true; }
	void CancelForBodyRemoval(AActor* Target);
	AActor* GetAnchorActor() const { return Anchor.Get(); }
	const FLSShellShot& GetShot() const { return Shot; }
	class ALSShellGravityField* GetSourceGravityField() const;
	void SetSourceGravityField(class ALSShellGravityField* Field);
	virtual void Tick(float DeltaSeconds) override;
	bool IsPending() const { return bPending; }
	float GetPendingSeconds() const;
	ELSShellModule GetPendingModule() const { return Shot.Second; }
	void ActivateSecond();
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	enum class EFollowupBinding : uint8 { ExistingAnchor, LastPoint, GatherOutput };
	void UpdateAnchor();
	UFUNCTION() void OnAnchorDied(const FLSDamageResult& Damage);
	TWeakObjectPtr<class ALSShellExperimentCoordinator> Coordinator;
	TWeakObjectPtr<AActor> Anchor;
	TWeakObjectPtr<AActor> LastAnchorOwner;
	TWeakObjectPtr<class ALSShellGravityField> SourceGravityField;
	FLSShellShot Shot;
	FHitResult Impact;
	FVector InitialImpactPoint = FVector::ZeroVector;
	FVector LocalPoint, LocalNormal;
	uint64 AnchorBodyId = 0;
	uint32 AnchorGeneration = 0;
	FTimerHandle Delay;
	bool bPending = false;
	bool bCombatImpact = false;
	bool bGeneratedBodyFailed = false;
	EFollowupBinding FollowupBinding = EFollowupBinding::ExistingAnchor;
};
