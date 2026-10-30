#include "Controls/LSSelectableComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "GameFramework/Actor.h"

ULSSelectableComponent::ULSSelectableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool ULSSelectableComponent::IsSelectable() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed() &&
	       (!Owner->FindComponentByClass<ULSHealthComponent>() || ULSCombatTargetLibrary::IsCombatAlive(Owner));
}

FName ULSSelectableComponent::GetSelectionType() const
{
	return SelectionType.IsNone() && GetOwner() ? GetOwner()->GetClass()->GetFName() : SelectionType;
}
