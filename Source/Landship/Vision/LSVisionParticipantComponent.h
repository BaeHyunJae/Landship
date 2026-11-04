#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Vision/LSVisionTypes.h"
#include "LSVisionParticipantComponent.generated.h"

/** 액터의 시야 참여 역할을 명시한다. 투사체를 포함한 일반 액터는 기본적으로 시야를 제공하지 않는다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSVisionParticipantComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	ULSVisionParticipantComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Vision")
	uint8 bProvidesSight : 1 = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Vision")
	uint8 bConcealedOutsideSight : 1 = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Vision")
	uint8 bTriggersEnemyDiscovery : 1 = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landship|Vision")
	uint8 bPublicCorePosition : 1 = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Vision")
	ELSMinimapMarkerKind MarkerKind = ELSMinimapMarkerKind::Obstacle;

	/** 성·병력의 기존 수치 초기화가 반경을 전달한다. 별도 기본 반경으로 대체하지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Landship|Vision", meta = (DevelopmentOnly))
	void SetSightRadius(float Radius);
	/** 비활성 상태에서는 시야·마커·최초 발견에 참여하지 않는다. 활성화 시 현재 가시 상태를 기준으로 등록한다. */
	void SetParticipationEnabled(bool bEnabled);
	bool IsParticipationEnabled() const { return bParticipationEnabled; }
	float GetSightRadius() const
	{
		return SightRadius;
	}
	bool IsFriendly() const;
	bool IsLivingCombatant() const;
	bool IsEnemyDiscoveryCandidate() const;
	bool ShouldConceal() const;
	void ApplySightVisibility(bool bVisible);
	void GetAttachedVisualActors(TArray<AActor*>& OutActors) const;

	/** 생략 가능한 공개 사망 알림에만 기대지 않도록 필수 사망 처리에서도 호출한다. */
	void NotifyCombatDeath();
	void SuspendForPool();
	void ResumeFromPool();

  protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
	void RegisterParticipation();
	bool bParticipationEnabled = true;
	float SightRadius = 0.0f;
	bool bOriginalHidden = false;
	bool bRegistered = false;
	bool bHiddenBySight = false;
	TMap<TWeakObjectPtr<AActor>, bool> ChildOriginalHidden;
};
