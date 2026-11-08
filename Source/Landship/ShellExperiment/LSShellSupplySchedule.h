#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShellExperiment/LSShellTransport.h"
#include "LSShellSupplySchedule.generated.h"

class ALSShellExperimentEnemy;
class UDataTable;
class UTextRenderComponent;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellSupplyRoute
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Start = FVector(-800, -1000, 70);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector End = FVector(700, -1000, 70);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FirstDepartureSeconds = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RepeatSeconds = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FLSShellCargoSlot> CargoSlots;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FLSShellCargoSlot> ChargedCargoSlots;
	/** 세 후보를 순서대로 실제 적재 형상으로 검사한다. 기존 화물을 치우거나 순간이동하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> UnloadOffsets = { FVector::ZeroVector, FVector(0, 550, 0), FVector(0, -550, 0) };
	/** 하역 뒤 빈차가 따라갈 월드 경유점이다. 비어 있으면 거점 앞에서 바깥 차선으로 돌아 동쪽 출구를 사용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> ExitWaypoints;
	/** 실제 하역점에서 검사할 네 방향 후보의 기준 오프셋이다. Z는 지면으로부터의 추가 높이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ReplenishmentOffset = FVector(0, 350, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SiegeDeploymentLocation = FVector(-700, -600, 140);
};

UCLASS(Blueprintable)
class LANDSHIP_API ALSShellSupplySchedule : public AActor
{
	GENERATED_BODY()
public:
	ALSShellSupplySchedule();
	virtual void Tick(float DeltaSeconds) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") FLSShellSupplyRoute RouteA;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") FLSShellSupplyRoute RouteB;
	/** 앞 단계에 배치한 A 차량을 첫 회차로 사용한다. 같은 회차의 차량을 또 만들지 않는다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TObjectPtr<ALSShellTransport> InitialRouteATransport;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TArray<TObjectPtr<ALSShellExperimentEnemy>> InitialAssaults;
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TObjectPtr<ALSShellExperimentEnemy> InitialHeavy;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TSubclassOf<ALSShellTransport> TransportClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TSubclassOf<ALSShellExperimentEnemy> EnemyClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") TObjectPtr<UDataTable> EnemyStatsTable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") int32 MaximumActiveDeliveriesPerRoute = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") float WarningSeconds = 8;
	/** 실제 발사 관찰로 갱신할 최장 비행 시간이다. 기본값은 검증 전 가설이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Schedule") float WorstObservedShotFlightSeconds = 2.9f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Schedule") float InputMarginSeconds = .5f;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Schedule") float NoticeSeconds = 5;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Schedule") float ReplenishmentRetrySeconds = 1;
	UPROPERTY(EditDefaultsOnly, Category = "Shell Experiment|Schedule") int32 MaximumReplenishmentAttempts = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") bool bArenaPressureEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") TArray<float> ArenaPressureTimes = { 12, 32, 52, 72 };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") TArray<FVector> ArenaPressureEntries = { FVector(-900, 400, 88), FVector(-900, -400, 88) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") int32 ArenaPressureEnemyCap = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") float ArenaPressureRetrySeconds = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") int32 ArenaPressureMaximumAttempts = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell Experiment|Arena Pressure") float ArenaPressureAlternateOffset = 150;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 DepartedCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 SkippedCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 ReplenishedCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") int32 FailedReplenishmentCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shell Experiment|Observation") FString ConfigurationError;
	UFUNCTION(BlueprintPure) FString GetStatusText() const;
	UFUNCTION(BlueprintPure) int32 GetActiveDeliveryCount(int32 RouteIndex) const;
	UFUNCTION(BlueprintPure) float GetRequiredWarningSeconds() const;
	/** 사망 전환으로 새 몸체가 기존 물자를 이어받았을 때 잔존 물자 제한의 참조만 교체한다. */
	void NotifyDeliveryActorReplaced(AActor* Original, AActor* Replacement);
	static int32 GetScheduledDepartureOrdinal(const FLSShellSupplyRoute& Route, double ElapsedSeconds);
protected:
	virtual void BeginPlay() override;
private:
	struct FLSActiveDelivery
	{
		int32 RouteIndex = 0;
		int32 Ordinal = 0;
		TWeakObjectPtr<ALSShellTransport> Vehicle;
		TArray<TWeakObjectPtr<AActor>> Resources;
		FVector UnloadLocation = FVector::ZeroVector;
		double NextExitAttemptSeconds = 0;
		double NextReplenishmentAttemptSeconds = 0;
		int32 ReplenishmentAttempts = 0;
		FString LastReplenishmentFailure;
		bool bReplenishmentHandled = false;
		bool bExitStarted = false;
		bool bExitBlocked = false;
	};
	struct FLSPressureAttempt
	{
		int32 EntryIndex = 0;
		int32 Attempts = 0;
		double NextAttempt = 0;
	};
	bool TryArenaPressure(int32 EntryIndex);
	void UpdateArenaPressure(double Elapsed);
	FString GetArenaPressureStatus(double Elapsed) const;
	FString ValidateConfiguration() const;
	bool IsLoadedPoseClear(FVector Position, const FLSShellSupplyRoute& Route, const TArray<FLSShellCargoSlot>& Cargo) const;
	bool IsLoadedRouteClear(const FLSShellSupplyRoute& Route, const TArray<FLSShellCargoSlot>& Cargo) const;
	bool FindAvailableDepartureRoute(int32 RouteIndex, FLSShellSupplyRoute& OutRoute) const;
	bool IsExitRouteClear(ALSShellTransport* Vehicle, const TArray<FVector>& Waypoints) const;
	TArray<FVector> BuildExitWaypoints(const ALSShellTransport* Vehicle, int32 RouteIndex, FVector UnloadLocation) const;
	bool HasGroundAlongSegment(FVector Start, FVector End, float BodyHalfHeight) const;
	void TryExit(FLSActiveDelivery& Delivery);
	bool TryDeparture(int32 RouteIndex, int32 Ordinal);
	bool TryReplenish(FLSActiveDelivery& Delivery);
	void ReportReplenishmentFailure(FLSActiveDelivery& Delivery, const FString& Reason, bool bTerminal = false);
	void AddDelivery(ALSShellTransport* Vehicle, int32 RouteIndex, int32 Ordinal);
	void SetNotice(int32 RouteIndex, const FString& Text, bool bTerminalReplenishmentFailure = false);
	void UpdatePresentation();
	FString GetRouteStatusText(int32 RouteIndex) const;
	static bool HasRemainingResources(const FLSActiveDelivery& Delivery);
	TArray<FLSActiveDelivery> Deliveries;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> RouteLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> PressureLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PressureMarks;
	TArray<FLSPressureAttempt> PressureAttempts;
	int32 NextPressureTime = 0;
	double BattleStartSeconds = -1;
	double NextDepartures[2] { 0, 16 };
	double NoticeExpires[2] { 0, 0 };
	int32 SuccessfulDepartures[2] { 0, 0 };
	FString Notices[2];
	bool bTerminalReplenishmentNotice[2] { false, false };
	bool bInitialAdopted = false;
};
