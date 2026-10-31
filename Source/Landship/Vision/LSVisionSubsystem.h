#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vision/LSVisionTypes.h"
#include "LSVisionSubsystem.generated.h"

class ULSVisionParticipantComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLSVisionSnapshotCommitted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLSFirstEnemyDiscovered);

/** 플레이어 측의 현재 시야만 관리한다. 적 AI의 탐지와 명중 피해 규칙은 소유하지 않는다. */
UCLASS(BlueprintType)
class LANDSHIP_API ULSVisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
  public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterParticipant(ULSVisionParticipantComponent* Participant);
	void UnregisterParticipant(ULSVisionParticipantComponent* Participant);
	void RequestRefresh();

	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	bool IsActorVisibleToFriendly(const AActor* Actor) const;
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	bool IsPointVisible(FVector Location) const;
	/** 순수 진영·생존 검사에 추가할 관찰자 규칙이다. 적과 비참여 액터의 기존 동작을 바꾸지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	static bool PassesFriendlySight(const AActor* Observer, const AActor* Target);
	void GetConcealedActors(TArray<AActor*>& OutActors) const;
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	TArray<FLSMinimapMarker> GetMinimapMarkers() const;
	const TArray<FLSSightCircle>& GetSightCircles() const
	{
		return SightCircles;
	}
	uint64 GetSnapshotGeneration() const
	{
		return SnapshotGeneration;
	}
	/** 명시적 전장 설정이 있는 플레이에서만 시야 규칙을 적용한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	bool IsVisionEnabled() const
	{
		return bVisionEnabled;
	}

	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	bool HasDiscoveredEnemy() const
	{
		return bEnemyDiscovered;
	}
	UFUNCTION(BlueprintPure, Category = "Landship|Vision")
	bool HasPendingDiscoveryCaption() const
	{
		return bEnemyDiscovered && !bDiscoveryCaptionDisplayed;
	}
	UFUNCTION(BlueprintCallable, Category = "Landship|Vision")
	void MarkDiscoveryCaptionDisplayed();

	UPROPERTY(BlueprintAssignable, Category = "Landship|Vision")
	FLSVisionSnapshotCommitted OnSnapshotCommittedEvent;
	UPROPERTY(BlueprintAssignable, Category = "Landship|Vision")
	FLSFirstEnemyDiscovered OnFirstEnemyDiscoveredEvent;

  protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

  private:
	void Refresh();
	TArray<TWeakObjectPtr<ULSVisionParticipantComponent>> Participants;
	TArray<FLSSightCircle> SightCircles;
	TSet<TWeakObjectPtr<AActor>> VisibleActors;
	TArray<FLSMinimapMarker> MinimapMarkers;
	TMap<TWeakObjectPtr<AActor>, FLSMinimapMarker> PublicCoreMarkers;
	float Elapsed = 0.0f;
	uint64 SnapshotGeneration = 0;
	bool bWorldStarted = false;
	bool bVisionEnabled = false;
	bool bRefreshPending = false;
	bool bRefreshing = false;
	bool bEnemyDiscovered = false;
	bool bDiscoveryCaptionDisplayed = false;

	float RefreshInterval = 0.1f;
};
