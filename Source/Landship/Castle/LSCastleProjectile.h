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
	/** Deferred 생성 중 한 번 준비한다. 곡사 충돌 오차를 지키기 위한 계산량 상한을 넘으면 거부하며, 성공했을 때만 FinishSpawning한다. */
	bool Launch(ELSCastleWeapon Weapon, const FLSCastleWeaponSettings& Settings, const FLSDamageRequest& Request,
	            const FVector& Aim, bool bFirstBlocking = false);
	virtual void Tick(float DeltaSeconds) override;
	bool IsInFlight() const { return bLaunched && !bResolved && !IsActorBeingDestroyed(); }
	FVector GetFlightOrigin() const { return LaunchLocation; }
	ELSCastleWeapon GetWeapon() const { return Kind; }
	/** 현재 살아 있는 탄의 실제 월드 위치를 반환한다. 산탄은 액터 원점이 아니라 각 메시 위치다. */
	void GetFlightPositions(TArray<FVector>& OutPositions) const;
	void GetFlightSegments(TArray<TPair<FVector, FVector>>& OutSegments) const;

  protected:
	virtual void BeginPlay() override;

  private:
	bool IsLivingCombatant(AActor* Target) const;
	FLSDamageRequest MakeHitRequest() const;
	bool FindPelletHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	bool FindCannonCoverHit(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	bool HasCannonBlastLine(AActor* Target) const;
	FVector GetCannonPosition(float NormalizedTime) const;
	void Detonate();
	void ApplyHit(AActor* Target, float Damage, const FVector& Direction, bool bKnockback, const FVector& Impact,
	              const FVector& IncomingDirection);
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Bullets;
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Source;
	UPROPERTY(Transient)
	FLSDamageRequest ShotRequest;
	UPROPERTY(Transient)
	FLSCastleWeaponSettings ShotSettings;
	TArray<FVector> Directions;
	TArray<FVector> PreviousPelletPositions;
	TArray<bool> ActivePellets;
	TSet<TWeakObjectPtr<AActor>> PushedTargets;
	FVector LaunchLocation = FVector::ZeroVector;
	FVector PreviousFlightPosition = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	ELSCastleWeapon Kind = ELSCastleWeapon::Cannon;
	float FlightTime = 0;
	float Elapsed = 0;
	int32 CannonArcSteps = 1;
	bool bLaunched = false;
	bool bResolved = false;
	bool bHitsFirstBlocking = false;
};
