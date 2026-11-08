#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LSShellExperimentTypes.h"
#include "LSShellProjectile.generated.h"

UCLASS()
class LANDSHIP_API ALSShellProjectile : public AActor
{
	GENERATED_BODY()
public:
	ALSShellProjectile();
	void Launch(class ALSShellExperimentCoordinator* OwnerCoordinator, const FLSShellShot& InShot, FVector Start, FVector Direction);
	virtual void Tick(float DeltaSeconds) override;
	static bool SweepFlight(UWorld* World, const FVector& Start, const FVector& End, AActor* Source, FHitResult& Hit, const UPrimitiveComponent* InitialSurface = nullptr);
private:
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Visual;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Trail;
	TWeakObjectPtr<class ALSShellExperimentCoordinator> Coordinator;
	FLSShellShot Shot;
	FVector Velocity = FVector::ZeroVector;
	float DistanceTravelled = 0, Age = 0;
	bool bLaunched = false;
};
