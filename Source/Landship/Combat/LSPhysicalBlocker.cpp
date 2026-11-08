#include "Combat/LSPhysicalBlocker.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

bool LSPhysicalBlocker::IsBlockingBody(const UPrimitiveComponent* Body)
{
	if (!IsValid(Body) || !Body->IsRegistered() || !Body->IsQueryCollisionEnabled() ||
		Body->GetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile) != ECR_Block)
		return false;
	AActor* Owner = Body->GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed()) return false;
	if (const auto* Provider = Cast<ILSPhysicalBlocker>(Owner); Provider && Provider->GetPhysicalBlockingBody() == Body)
		return true;
	TInlineComponentArray<UActorComponent*> Components(Owner);
	for (const UActorComponent* Component : Components)
		if (IsValid(Component) && Component->IsRegistered())
			if (const auto* Provider = Cast<ILSPhysicalBlocker>(Component); Provider && Provider->GetPhysicalBlockingBody() == Body)
				return true;
	return false;
}

UPrimitiveComponent* LSPhysicalBlocker::FindBlockingBody(const AActor* Actor)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return nullptr;
	if (const auto* Provider = Cast<ILSPhysicalBlocker>(Actor))
		if (UPrimitiveComponent* Candidate = Provider->GetPhysicalBlockingBody(); IsBlockingBody(Candidate) && Candidate->GetOwner() == Actor)
			return Candidate;
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (const UActorComponent* Component : Components)
		if (IsValid(Component) && Component->IsRegistered())
			if (const auto* Provider = Cast<ILSPhysicalBlocker>(Component))
				if (UPrimitiveComponent* Candidate = Provider->GetPhysicalBlockingBody(); IsBlockingBody(Candidate) && Candidate->GetOwner() == Actor)
					return Candidate;
	return nullptr;
}
