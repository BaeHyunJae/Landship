#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ALSAutoBattleCastle::ALSAutoBattleCastle()
{
	Weapons = CreateDefaultSubobject<ULSCastleWeaponComponent>(TEXT("Weapons"));
}

void ALSAutoBattleCastle::EnablePlayerDrive()
{
	auto* Movement = GetCharacterMovement();
	bPlayerDriveEnabled = Movement->MaxWalkSpeed > 0;
	Movement->MaxAcceleration = DriveAcceleration;
	Movement->BrakingDecelerationWalking = DriveBrakingDeceleration;
	Movement->bUseSeparateBrakingFriction = true;
	Movement->BrakingFriction = 0.0f;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->bRunPhysicsWithNoController = true;
	Movement->bCanWalkOffLedges = false;
	bUseControllerRotationYaw = false;
}

void ALSAutoBattleCastle::Drive(float Throttle, float Steering, float DeltaSeconds)
{
	auto* Movement = GetCharacterMovement();
	if (!bPlayerDriveEnabled || !Movement->IsMovingOnGround())
		return;
	if (Steering != 0)
	{
		const float SpeedRatio = FMath::Clamp(static_cast<float>(Movement->Velocity.Size2D()) / ForwardSpeed, 0.0f, 1.0f);
		const float TurnSpeed = FMath::Lerp(StationaryTurnSpeed, FullSpeedTurnSpeed, SpeedRatio);
		AddActorWorldRotation(FRotator(0, Steering * TurnSpeed * DeltaSeconds, 0));
	}
	const bool bReversing = Throttle != 0 &&
	    FVector::DotProduct(Movement->Velocity, GetActorForwardVector() * Throttle) < -1.0f;
	Movement->MaxAcceleration = DriveAcceleration;
	if (Throttle == 0 || bReversing)
		return;
	Movement->MaxWalkSpeed = Throttle < 0 ? ReverseSpeed : ForwardSpeed;
	AddMovementInput(GetActorForwardVector(), Throttle, true);
}
