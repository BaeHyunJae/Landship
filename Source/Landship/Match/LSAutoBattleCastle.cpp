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
	ForwardSpeed = Movement->MaxWalkSpeed;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->bRunPhysicsWithNoController = true;
	Movement->bCanWalkOffLedges = false;
	bUseControllerRotationYaw = false;
}

void ALSAutoBattleCastle::Drive(float Throttle, float Steering, float DeltaSeconds)
{
	if (ForwardSpeed <= 0 || !GetCharacterMovement()->IsMovingOnGround())
		return;
	if (Steering != 0)
		AddActorWorldRotation(FRotator(0, Steering * TurnSpeed * DeltaSeconds, 0));
	if (Throttle == 0)
		return;
	GetCharacterMovement()->MaxWalkSpeed = Throttle < 0 ? ForwardSpeed * ReverseSpeedRatio : ForwardSpeed;
	AddMovementInput(GetActorForwardVector(), Throttle, true);
}
