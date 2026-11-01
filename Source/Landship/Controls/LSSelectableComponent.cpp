#include "Controls/LSSelectableComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

ULSSelectableComponent::ULSSelectableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool ULSSelectableComponent::IsSelectable() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	const ULSVisionSubsystem* Vision = World ? World->GetSubsystem<ULSVisionSubsystem>() : nullptr;
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() &&
		   (!Vision || Vision->IsActorVisibleToFriendly(Owner)) &&
		   (!Owner->FindComponentByClass<ULSHealthComponent>() ||
			ULSCombatTargetLibrary::IsCombatAlive(Owner));
}

FName ULSSelectableComponent::GetSelectionType() const
{
	return SelectionType.IsNone() && GetOwner() ? GetOwner()->GetClass()->GetFName() : SelectionType;
}
