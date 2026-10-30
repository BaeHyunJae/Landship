#include "Controls/LSSelectableComponent.h"
#include "Combat/LSBasicAttackParticipant.h"
#include "GameFramework/Actor.h"

ULSSelectableComponent::ULSSelectableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool ULSSelectableComponent::IsSelectable() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) && !Owner->IsActorBeingDestroyed()
		&& (!Owner->Implements<ULSBasicAttackParticipant>() || ILSBasicAttackParticipant::Execute_IsCombatAlive(Owner));
}

FName ULSSelectableComponent::GetSelectionType() const
{
	return SelectionType.IsNone() && GetOwner() ? GetOwner()->GetClass()->GetFName() : SelectionType;
}
