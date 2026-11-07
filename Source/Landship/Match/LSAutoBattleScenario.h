#pragma once

#include "GameFramework/Actor.h"
#include "Curves/CurveFloat.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSCastleWeaponData.h"
#include "LSAutoBattleScenario.generated.h"

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSAutoBattlePlacement
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELSTroopType Type = ELSTroopType::Spearman;
	/** 지면 좌표다. 액터 중심 높이는 생성 시 더한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Position = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Id = 0;
};

/** 자동 전투 맵에 하나만 배치한다. 편성·경제·전장 경계를 명시적으로 저작한다. */
UCLASS()
class LANDSHIP_API ALSAutoBattleScenario : public AActor
{
	GENERATED_BODY()
  public:
	ALSAutoBattleScenario();
	virtual void PostLoad() override;
	static int32 GetDefaultUnitCost(ELSTroopType Type);
	FString GetConfigurationError() const;
	/** 에디터 월드에서만 실행한다. 실패하면 EnemyFormation을 바꾸지 않는다. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Enemy Placement Tool",
	          meta = (DisplayName = "1. Load Saved Formation"))
	void LoadEnemyMarkers();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Enemy Placement Tool", meta = (DisplayName = "2. Add Enemy"))
	void AddEnemyMarker();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Enemy Placement Tool",
	          meta = (DisplayName = "3. Apply Formation"))
	void ApplyEnemyMarkers();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Enemy Placement Tool",
	          meta = (DisplayName = "4. Hide Markers"))
	void HideEnemyMarkers();
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bEnemyMarkersLoaded = false;
	UPROPERTY(EditAnywhere, Category = "Enemy Placement Tool")
	ELSTroopType PlacementTroopType = ELSTroopType::Spearman;
	/** 새 마커를 놓을 지면 좌표. 차 있으면 가까운 빈 지면을 찾는다. */
	UPROPERTY(EditAnywhere, Category = "Enemy Placement Tool")
	FVector PlacementGround = FVector(700, 0, 0);
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Enemy Placement Tool")
	FString PlacementToolStatus =
	    TEXT("Load Saved Formation → Add / Move / Alt+Drag / Delete → Apply Formation → Ctrl+S");
#endif
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector2D BoundsMin = FVector2D(-2400, -1800);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector2D BoundsMax = FVector2D(2400, 1800);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector2D DeploymentMin = FVector2D(-1800, -1400);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector2D DeploymentMax = FVector2D(-300, 1400);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector AllyCastleGround = FVector(-1550, 0, 0);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector EnemyCastleGround = FVector(1700, 0, 0);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FVector CameraLocation = FVector(-2000, -1600, 1800);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	FRotator CameraRotation = FRotator(-38, 35, 0);
	UPROPERTY(EditAnywhere, Category = "Scenario")
	TMap<ELSTroopType, int32> UnitCosts;
	/** 공격 병종이 새 표적을 발견하는 거리(cm). 실제 감지 거리는 공격 사거리 이상이다. */
	UPROPERTY(EditAnywhere, Category = "Scenario|Engagement")
	TMap<ELSTroopType, float> EnemyDetectionRanges;
	/** 병력 경사 각도(도)별 이동 속도 배율이다. 양수는 오르막, 음수는 내리막이다. */
	UPROPERTY(EditAnywhere, Category = "Scenario|Slope")
	FRuntimeFloatCurve TroopSlopeSpeed;
	/** 아군과 적군 성에 공통으로 적용하는 경사 각도별 속도 배율이다. */
	UPROPERTY(EditAnywhere, Category = "Scenario|Slope")
	FRuntimeFloatCurve CastleSlopeSpeed;
	/** 기존 표적을 놓기 전 감지 거리에 더하는 여유 거리(cm). */
	UPROPERTY(EditAnywhere, Category = "Scenario|Engagement", meta = (ClampMin = "0", Units = "cm"))
	float TargetRetentionPadding = 250.0f;
	UPROPERTY(EditAnywhere, Category = "Scenario")
	TArray<FLSAutoBattlePlacement> EnemyFormation;
	UPROPERTY(EditAnywhere, Category = "Scenario")
	ELSCastleWeapon EnemyWeapon = ELSCastleWeapon::Cannon;
};
