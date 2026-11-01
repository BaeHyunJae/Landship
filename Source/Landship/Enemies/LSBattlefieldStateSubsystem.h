#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/LSHealthTypes.h"
#include "LSBattlefieldStateSubsystem.generated.h"

class ALSOutpost;
class ULSHealthComponent;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSPlayerCastleState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	TWeakObjectPtr<AActor> Castle;
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	uint8 bIsDead : 1 = false;
};

/** 액터 제거 뒤에도 이름과 함락 위치를 조회할 수 있다. */
USTRUCT(BlueprintType)
struct LANDSHIP_API FLSOutpostCaptureSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	TWeakObjectPtr<ALSOutpost> Outpost;
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	FName OutpostName;
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Battlefield")
	uint8 bIsCoreOutpost : 1 = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLSOnCoreOutpostCaptured, const FLSOutpostCaptureSnapshot&, Capture);

/** 명시적으로 지정한 전장 대상과 이미 관찰한 종료 사실만 보관한다. */
UCLASS()
class LANDSHIP_API ULSBattlefieldStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	bool IsSetupValid() const { return bSetupValid; }
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	bool TryGetPlayerCastleState(FLSPlayerCastleState& Out) const;
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	ALSOutpost* GetCoreOutpost() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	bool IsBattleEndedForEnemies() const;
	/** 성 참조가 사라진 뒤에도 이미 관찰한 실제 사망만 반환한다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	bool HasObservedPlayerCastleDeath() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield")
	bool TryGetCoreCapture(FLSOutpostCaptureSnapshot& Out) const;

	/** 거점의 필수 사망 준비가 외부 콜백을 일으키기 전에 호출한다. */
	void RecordOutpostCaptured(ALSOutpost* Outpost);

	/** 필수 사망 정리가 반환된 다음 틱에 한 번 발생한다. */
	UPROPERTY(BlueprintAssignable, Category = "Landship|Battlefield")
	FLSOnCoreOutpostCaptured OnCoreOutpostCapturedEvent;
private:
	UFUNCTION()
	void OnCastleDied(const FLSDamageResult& Damage);
	UFUNCTION()
	void OnCastleEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);
	void ObserveCastleDeath() const;
	void BroadcastCoreCapture();

	TWeakObjectPtr<AActor> PlayerCastle;
	TWeakObjectPtr<ULSHealthComponent> CastleHealth;
	TWeakObjectPtr<ALSOutpost> CoreOutpost;
	FLSOutpostCaptureSnapshot CoreCapture;
	FTimerHandle CaptureNotificationTimer;
	bool bSetupValid = false;
	mutable bool bCastleDeathObserved = false;
	bool bCoreCaptured = false;
	bool bCaptureBroadcast = false;
};
