#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSHealthTypes.h"
#include "Match/LSGameState.h"
#include "LSShellExperimentTypes.h"
#include "LSShellChargeGraph.h"
#include "LSShellGeneratedMaterial.h"
#include "LSShellExperimentCoordinator.generated.h"

class ALSCastle;
class ALSOutpost;
class ALSShellEffectInstance;

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellExperimentCoordinator : public AActor
{
	GENERATED_BODY()
public:
	ALSShellExperimentCoordinator();
	static ALSShellExperimentCoordinator* Find(const UWorld* World);
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite) TObjectPtr<ALSCastle> PlayerCastle;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite) TObjectPtr<ALSOutpost> CoreOutpost;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLSShellTuning Tuning;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ELSShellRuleProfile RuleProfile = ELSShellRuleProfile::Legacy;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ELSShellSupplyStage SupplyStage = ELSShellSupplyStage::Physics;
	/** 개선 맵의 단계 저작에서 관리하는 초기 선택지다. Legacy에는 적용하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<ELSShellModule> AvailableModules = {ELSShellModule::Blast, ELSShellModule::Thrust};
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ELSShellModule InitialFirst = ELSShellModule::Thrust;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ELSShellModule InitialSecond = ELSShellModule::Blast;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLSShellSupplyTuning SupplyTuning;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning ThrustFeedback = FLSShellFeedbackTuning(.8f, .28f, .12f, 1.45f, 140, 220);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning ContactFeedback = FLSShellFeedbackTuning(1.1f, .24f, .06f, .6f, 300);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning BlastFeedback = FLSShellFeedbackTuning(1.3f, .42f, .1f, 1.1f, 500);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning ChainFeedback = FLSShellFeedbackTuning(1.8f, .75f, .14f, .8f, 850);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning ChargeFeedback = FLSShellFeedbackTuning(.65f, .4f, .12f, 1.6f, 180, 250);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning DischargeFeedback = FLSShellFeedbackTuning(.8f, .25f, .08f, 1.8f, 220, 250);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning CoolingFeedback = FLSShellFeedbackTuning(1.1f, .6f, .2f, .75f, 220, 600);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") FLSShellFeedbackTuning FailureFeedback = FLSShellFeedbackTuning(.35f, .8f, .08f, .65f, 90, 140);
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") float ThrustCueRadius = 60;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") float ContactCueRadius = 140;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") float ContactCueMinimumSpeed = 30;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") float ContactCueReferenceMassKg = 300;
	UPROPERTY(EditAnywhere, Category = "Shell Experiment|Feedback") float SupplyRecoilScale = 1.8f;
	UFUNCTION(BlueprintPure) bool IsSupplyExperiment() const { return RuleProfile == ELSShellRuleProfile::SupplyStageOne; }
	UFUNCTION(BlueprintPure) bool IsArenaExperiment() const { return IsSupplyExperiment() && SupplyTuning.Arena.bEnabled; }
	float GetConfiguredFollowupDelay() const;
	float GetMaximumFollowupDelay() const;
	bool GetPredictedChargeTarget(FVector Aim, FVector& OutPoint) const;
	void NotifyChargeFailure(FVector Point, const FString& Reason);
	UFUNCTION(BlueprintPure) bool CanSelectModule(ELSShellModule Module) const;
	UFUNCTION(BlueprintPure) FString GetTargetFailureText() const;
	bool GetPendingEffectLocation(FVector& OutLocation) const;
	TArray<FVector> GetShieldPoints() const;
	void NotifyMissingTarget();
	void CaptureShotContext(FLSShellShot& Shot, AActor* Source = nullptr) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnableEvents = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> ConvoySpawnPoints;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ConvoyApproachPoint = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<class ALSShellExperimentEnemy> CarrierClass;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) float EventCandidateTime = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) bool bEventAttempted = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly) bool bEventExecuted = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> FireSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> ExplosionSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<class USoundBase> ImpactSound;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class ULSShellLauncherVisualComponent> LauncherVisual;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 ShellHitCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 ContactCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 DetonationCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 ModuleCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 DischargeCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float LastThrustGameTime = -1;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float LastContactGameTime = -1;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float CoreDamageCommitted = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FVector LastShellImpact;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FString LastContactActors;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float LastContactSpeed = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") float LastContactStepSeconds = 0;
#if WITH_EDITORONLY_DATA
	/** 관찰 도구가 첫 배치로 돌려놓고 실제 FireAt 경로를 한 번 실행할 대상이다. 체력·재장전은 바꾸지 않는다. */
	UPROPERTY(EditAnywhere, Transient, Category = "Shell Experiment|Observation") TObjectPtr<class ALSShellExperimentEnemy> ObservationTarget;
	UPROPERTY(EditAnywhere, Transient, Category = "Shell Experiment|Observation") bool bRequestContactObservation = false;
	/** 살아 있는 동력로 위치에 실제 장전·발사 규칙으로 사격한다. 체력은 직접 바꾸지 않는다. */
	UPROPERTY(EditAnywhere, Transient, Category = "Shell Experiment|Observation") bool bRequestCoreShotObservation = false;
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Shell Experiment|Observation") bool bObservationShotAccepted = false;
#endif
	UFUNCTION(BlueprintPure) bool IsBattleActive() const;
	UFUNCTION(BlueprintCallable) bool FireAt(FVector Aim);
	UFUNCTION(BlueprintCallable) void SetComposing(bool bOpen);
	UFUNCTION(BlueprintCallable) void CancelCompose();
	UFUNCTION(BlueprintCallable) void SwapModules();
	UFUNCTION(BlueprintCallable) void SelectModule(int32 Slot, ELSShellModule Module);
	UFUNCTION(BlueprintPure) ELSShellModule GetModule(int32 Slot) const;
	UFUNCTION(BlueprintPure) bool IsComposing() const { return bComposing; }
	UFUNCTION(BlueprintPure) bool IsSplitUnlocked() const { return bSplitUnlocked; }
	UFUNCTION(BlueprintPure) float GetReloadFraction() const;
	UFUNCTION(BlueprintPure) bool CanManualTrigger() const;
	UFUNCTION(BlueprintCallable) void TriggerPending();
	UFUNCTION(BlueprintCallable) void RestartExperiment();
	UFUNCTION(BlueprintPure) FString GetStatusText() const;
	UFUNCTION(BlueprintPure) FString GetEventText() const;
	UFUNCTION(BlueprintPure) FString GetMaterialStatusText() const;
	void SetSupplyWarningText(const FString& Text) { if (IsBattleActive()) EventText = Text; }
	float ApplyCharge(AActor* Target, const FLSShellShot& Shot, float Amount);
	AActor* ResolveBodyTarget(AActor* Target) const;
	void RemapPhysicalBody(class ULSShellReactiveComponent* Previous, class ULSShellReactiveComponent* Replacement);
	class ULSShellReactiveComponent* ResolveCurrentBody(class ULSShellReactiveComponent* Previous) const;
	void HandleMaterialDeath(AActor* Target);
	bool RecallGeneratedMaterial(AActor* Target);
	UFUNCTION(BlueprintPure) FString GetReactionText() const;
	UFUNCTION(BlueprintPure) FString GetPendingEffectText() const;
	void SetAimPoint(FVector Aim);
	UFUNCTION(BlueprintPure) FLSHealthSnapshot GetCastleHealth() const;
	UFUNCTION(BlueprintPure) FLSHealthSnapshot GetCoreHealth() const;
	UFUNCTION(BlueprintPure) FString GetConfigurationError() const { return ConfigurationError; }
	void QueueExplosion(const FLSShellExplosion& Explosion);
	void QueueContact(AActor* Moving, AActor* Other, FVector Point, FVector Normal, FVector Velocity, FVector OtherVelocity, uint64 ContactEpoch = 0);
	void QueueShellHit(const FLSShellShot& Shot, const FHitResult& Hit);
	/** 개선 규칙의 위치 기반 후행 추진을 값으로 예약한다. 발사 방향·범위·총충격은 Shot의 값을 사용한다. */
	void QueuePointThrust(FVector Point, const FLSShellShot& Shot);
	void QueueGatherFollowup(ELSShellModule Module, const FLSShellShot& Shot, const FLSShellGatherOutput& Output);
	void ExecuteModule(ELSShellModule Module, FVector Point, const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact = false, ALSShellEffectInstance* Instance = nullptr);
	void SetManualTarget(ALSShellEffectInstance* Instance);
	void ClearManualTarget(ALSShellEffectInstance* Instance);
	void NotifyHeavyDefeated();
	void ShowPulse(FVector Point, float Radius, FLinearColor Color, bool bLarge = false);
	void ApplyExperimentDamage(AActor* Receiver, float Amount, const FLSShellShot& Shot, FVector Point, FVector Direction, ELSShellDamageCause Cause);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FLSShellSplitRuntime;
	void ShowSupplyCue(ELSShellCueStyle Style, FVector Point, FVector Direction, float Radius = 0, float StrengthScale = 1,
		ELSShellDetonationTrigger Trigger = ELSShellDetonationTrigger::Damage, FVector TriggerOrigin = FVector::ZeroVector,
		bool bHasTriggerOrigin = false, UPrimitiveComponent* TargetBody = nullptr);
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	struct FQueuedReaction
	{
		enum class EKind { Explosion, ShellHit, Contact, PointThrust, Cooling, GatherFollowup } Kind = EKind::Explosion;
		FLSShellExplosion Explosion;
		FLSShellShot Shot;
		FHitResult Hit;
		TWeakObjectPtr<AActor> Moving, Other;
		FVector Velocity = FVector::ZeroVector, OtherVelocity = FVector::ZeroVector;
		uint64 MovingBodyId = 0, OtherBodyId = 0;
		uint32 MovingGeneration = 0, OtherGeneration = 0;
		float StepSeconds = 0;
		float GameTime = 0;
		uint64 ContactEpoch = 0;
		uint64 ChargeSequence = 0;
		float DischargeBudget = 0;
		float DischargeMinimumSpeed = 30;
		FLSShellChargeShare ChargeShare;
		TArray<FVector> ChargePath;
		bool bCombatImpact = false;
		bool bThrustPropelled = false;
		ELSShellModule GatherModule = ELSShellModule::Blast;
		FLSShellGatherOutput GatherOutput;
	};
	void ResolveExplosion(const FLSShellExplosion& Explosion);
	void ResolveSupplyExplosion(const FLSShellExplosion& Explosion);
	void ResolvePointThrust(FVector Point, const FLSShellShot& Shot);
	void ResolveGatherFollowup(ELSShellModule Module, const FLSShellShot& Shot, const FLSShellGatherOutput& Output);
	void QueueCooling(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit);
	void ResolveCooling(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit);
	void ResolveArenaSurfaceContact(const FQueuedReaction& Event, ULSShellReactiveComponent* Moving, AActor* Surface);
	TArray<FQueuedReaction> ReserveContactDischarges(const FQueuedReaction& Contact);
	void ResolveDischarge(const FQueuedReaction& Event);
	void ResolveSupplyContact(const FQueuedReaction& Event);
	void ResolveShellHit(const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact);
	void SpawnChildren(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact, ALSShellEffectInstance* Instance = nullptr);
	void UpdateEvent();
	bool GetConvoyEntries(TArray<FVector>& Entries) const;
	class ALSShellExperimentEnemy* FindLivingHeavy() const;
	void ResolveContact(const FQueuedReaction& Reaction);
	bool ComputeShotPose(FVector Aim, FVector& Origin, FVector& Muzzle, FVector& Direction) const;
	void SetReactionText(const FString& Text, int32 Priority = 0);
	void SetTargetFailure(const FString& Text);
	void ShowDestroyedStructure(AActor* Target);
	UFUNCTION() void OnBattleEnded(ELSBattleOutcome Outcome);
	TArray<FQueuedReaction> Reactions;
	TSet<uint64> SeenContactEpochs;
	mutable uint64 NextShotId = 0;
	mutable uint64 NextSourceId = 0;
	mutable TMap<TWeakObjectPtr<AActor>, uint64> SourceIdentities;
	struct FRemappedSourceIdentity
	{
		uint64 SourceId = 0;
		ELSFaction Faction = ELSFaction::Ally;
	};
	TMap<TWeakObjectPtr<AActor>, FRemappedSourceIdentity> RemappedSourceIdentities;
	uint64 ChargeContactSequence = 0;
	uint64 NextChargePacketId = 0;
	uint64 NextEndpointId = (uint64(1) << 63);
	TMap<TWeakObjectPtr<UPrimitiveComponent>, uint64> EndpointIdentities;
	TMap<TWeakObjectPtr<class ULSShellReactiveComponent>, TWeakObjectPtr<class ULSShellReactiveComponent>> BodyReplacements;
	TMap<uint64, TWeakObjectPtr<class ULSShellReactiveComponent>> RemappedBodyIdentities;
	FLSShellGeneratedMaterialRegistry GeneratedMaterials;
	TWeakObjectPtr<ALSShellEffectInstance> ManualTarget;
	ELSShellModule First = ELSShellModule::Gravity, Second = ELSShellModule::Blast;
	ELSShellModule PendingFirst = ELSShellModule::Gravity, PendingSecond = ELSShellModule::Blast;
	float NextFireTime = 0;
	bool bComposing = false, bSplitUnlocked = false, bStopped = false;
	FString ConfigurationError, EventText;
	FString ReactionText;
	float ReactionUntil = 0;
	int32 ReactionPriority = 0;
	FString TargetFailureText;
	float TargetFailureUntil = 0;
	TArray<FVector> ShieldPoints;
	float ShieldUntil = 0;
	int32 BurstDetonations = 0;
	enum class EEvent { None, Convoy, Offensive } PendingEvent = EEvent::None;
	TWeakObjectPtr<class ALSShellExperimentEnemy> EventHeavy;
	float EventWarningUntil = 0;
	float EventTextUntil = 0;
};
