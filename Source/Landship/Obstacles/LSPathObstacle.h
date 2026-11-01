#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/LSFactionInterface.h"
#include "LSPathObstacle.generated.h"

class UPrimitiveComponent;
class ULSHealthComponent;
class ULSVisionParticipantComponent;
class UStaticMeshComponent;
class ULSSelectableComponent;

UCLASS(Abstract, Blueprintable)
class LANDSHIP_API ALSPathObstacle : public AActor, public ILSFactionInterface
{
	GENERATED_BODY()
  public:
	ALSPathObstacle();
	virtual ELSFaction GetFaction() const override { return ELSFaction::Enemy; }
	UPrimitiveComponent* GetObstacleBody() const { return ObstacleBody; }
	ULSHealthComponent* GetHealthComponent() const { return Health; }
	ULSVisionParticipantComponent* GetVisionComponent() const { return Vision; }
	virtual bool CanBeAutomaticallyAcquired() const { return false; }
	virtual bool IsObstacleActive() const;
	virtual FVector GetInteractionLocation() const;
	virtual FString GetConfigurationError() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
  protected:
	virtual void BeginPlay() override;
	virtual void PrepareForDeath();
	void DisableObstacle();
	void SetObstacleBody(UPrimitiveComponent* Body, bool bBlocksMovement);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPrimitiveComponent> ObstacleBody;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSVisionParticipantComponent> Vision;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ULSSelectableComponent> Selection;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0.01")) float MaxHealth = 500;
};
