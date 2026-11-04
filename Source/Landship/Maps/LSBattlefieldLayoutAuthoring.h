#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSBattlefieldLayoutAuthoring.generated.h"

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSBattlefieldLayoutActorEntry
{
	GENERATED_BODY()

	/** 배치안이 액터를 찾는 고유 이름이다. 액터 이름을 바꿔도 이 값은 유지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	FName ActorId;

	/** 같은 월드에 배치된 액터를 지정한다. 위치·회전·크기 외의 속성은 바꾸지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TObjectPtr<AActor> Actor = nullptr;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSBattlefieldLayoutPreset
{
	GENERATED_BODY()

	/** 선택 목록에 표시할 고유 이름이다. 비워 두거나 다른 배치안과 중복할 수 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	FName PresetId;

	/** 등록한 모든 ActorId의 월드 위치·회전·크기를 빠짐없이 지정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TMap<FName, FTransform> Placements;
};

/** 에디터에서 고정된 액터들의 배치만 전환한다. 쿠킹에서 제외되며 플레이 중에는 적용하지 않는다. */
UCLASS()
class LANDSHIP_API ALSBattlefieldLayoutAuthoring : public AActor
{
	GENERATED_BODY()

  public:
	ALSBattlefieldLayoutAuthoring();

	/** 다른 액터와 부착 관계가 없는 액터만 등록한다. 같은 액터를 두 번 등록할 수 없다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TArray<FLSBattlefieldLayoutActorEntry> Registry;

	/** 각 배치안은 등록된 모든 액터의 월드 변환을 포함해야 한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TArray<FLSBattlefieldLayoutPreset> Presets;

	/** 봉쇄선·수비 기준점은 지형 및 진입 영역과 함께 고정하며 배치안에서 이동하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TArray<FName> FixedActorIds;

	/** 지역이 있는 전장은 배치안의 병력·장애물 이동이 지역 조건을 유지하는지도 검사한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout")
	TObjectPtr<class ALSBattlefieldEncounterCoordinator> EncounterCoordinator;

	/** 다음에 적용할 배치안이다. 선택만으로 현재 배치가 바뀌지는 않으며 Apply Selected Preset으로 적용한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landship|Battlefield Layout", meta = (GetOptions = "GetPresetOptions"))
	FName SelectedPreset;

	UFUNCTION()
	TArray<FString> GetPresetOptions() const;

	/** 모든 등록 액터의 월드 변환과 일치하는 첫 배치안을 반환한다. 일치하지 않거나 등록 대상·배치안 데이터가 잘못되었으면 None이다. 선택과 플레이 상태는 판정에 영향을 주지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield Layout")
	FName GetMatchingPreset() const;

	/** 전체 설정 검사를 통과한 경우에만 선택한 배치를 적용한다. 실패하면 어떤 액터도 바꾸지 않는다. */
	UFUNCTION(CallInEditor, Category = "Landship|Battlefield Layout")
	void ApplySelectedPreset();

	/** 현재 일치하는 배치안과 다음에 적용할 선택, 설정 오류를 로그에 출력한다. 액터를 바꾸지 않는다. */
	UFUNCTION(CallInEditor, Category = "Landship|Battlefield Layout")
	void ValidateLayout();

	/** 등록 대상·배치안·선택이 유효하면 빈 문자열, 아니면 첫 오류를 반환한다. 월드 유형과 플레이 상태는 검사하지 않는다. */
	UFUNCTION(BlueprintPure, Category = "Landship|Battlefield Layout")
	FString GetConfigurationError() const;

	/** 데이터가 유효하며 플레이 중이 아닌 에디터 월드일 때만 true다. 실패하면 OutError에 첫 원인을 반환한다. */
	bool CanApplyPreset(FName PresetId, FString& OutError) const;

	virtual bool IsEditorOnly() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

  private:
	bool ValidateConfiguration(FName PresetId, FString& OutError) const;
};
