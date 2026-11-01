#pragma once

#include "GameFramework/Actor.h"
#include "Castle/LSCastleWeaponData.h"
#include "Combat/LSHealthTypes.h"
#include "LSCastleProjectile.generated.h"

class UStaticMeshComponent;

/** 곡사 포탄 또는 산탄 한 발의 탄환 묶음. 발사 수치와 진영을 보관한다. */
UCLASS(NotPlaceable)
class LANDSHIP_API ALSCastleProjectile : public AActor
{
	GENERATED_BODY()
  public:
	ALSCastleProjectile();
	/** Deferred 생성 중 한 번 준비하고, 성공했을 때만 FinishSpawning한다. */
	bool Launch(ELSCastleWeapon Weapon, const FLSCastleWeaponSettings& Settings, const FLSDamageRequest& Request,
	            const FVector& Aim);
	virtual void Tick(float DeltaSeconds) override;

  protected:
	virtual void BeginPlay() override;

  private:
	bool IsEnemy(AActor* Target) const;
	bool FindPelletHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	void Detonate();
	void ApplyHit(AActor* Target, float Damage, const FVector& Direction, bool bKnockback);
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Bullets;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Source;
	UPROPERTY(Transient)
	FLSDamageRequest ShotRequest;
	UPROPERTY(Transient)
	FLSCastleWeaponSettings ShotSettings;
	TArray<FVector> Directions;
	TArray<bool> ActivePellets;
	TSet<TWeakObjectPtr<AActor>> PushedTargets;
	FVector LaunchLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	ELSCastleWeapon Kind = ELSCastleWeapon::Cannon;
	float FlightTime = 0;
	float Elapsed = 0;
	bool bLaunched = false;
	bool bResolved = false;
};
