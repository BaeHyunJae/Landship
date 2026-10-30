#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LSSelectableComponent.generated.h"

UENUM(BlueprintType)
enum class ELSSelectionKind : uint8 { Unit, Castle, Building };

/** 조작 권한은 진영 자체가 아니다. 진영 기능에서 플레이어 소유 여부를 연결한다. */
UCLASS(ClassGroup = (Landship), meta = (BlueprintSpawnableComponent))
class LANDSHIP_API ULSSelectableComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULSSelectableComponent();
	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	bool IsSelectable() const;
	UFUNCTION(BlueprintPure, Category = "Landship|Selection")
	bool CanPlayerControl() const { return bPlayerControlled && IsSelectable(); }
	UFUNCTION(BlueprintCallable, Category = "Landship|Selection")
	void SetPlayerControlled(bool bControlled) { bPlayerControlled = bControlled; }
	FName GetSelectionType() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Landship|Selection")
	ELSSelectionKind Kind = ELSSelectionKind::Unit;
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Selection")
	FName SelectionType;
private:
	UPROPERTY(EditDefaultsOnly, Category = "Landship|Selection")
	bool bPlayerControlled = true;
};
