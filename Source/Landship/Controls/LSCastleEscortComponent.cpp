#include "Controls/LSCastleEscortComponent.h"
#include "Combat/LSCombatTargetLibrary.h"

ULSCastleEscortComponent::ULSCastleEscortComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void ULSCastleEscortComponent::BeginPlay()
{
	Super::BeginPlay();
	TravelDirection = GetOwner()->GetActorForwardVector().GetSafeNormal2D();
}

void ULSCastleEscortComponent::ReleaseInvalidSlots(TArray<TWeakObjectPtr<ALSTroopUnit>>& Slots, bool bFront)
{
	for (auto& Slot : Slots)
	{
		const auto* Troop = Slot.Get();
		if (!ULSCombatTargetLibrary::IsCombatAlive(Troop) ||
		    Troop->GetFaction() != ELSFaction::Ally ||
		    FrontlineTroopTypes.Contains(Troop->GetTroopType()) != bFront)
			Slot.Reset();
	}
	// 중간 빈자리는 당겨 채우지 않아 다른 병력의 위치를 유지한다.
	while (!Slots.IsEmpty() && !Slots.Last().IsValid())
		Slots.Pop();
}

void ULSCastleEscortComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (FrontSlots.IsEmpty() && RearSlots.IsEmpty())
		return;
	const FVector Velocity = GetOwner()->GetVelocity();
	if (Velocity.SizeSquared2D() >= FMath::Square(DirectionSpeedThreshold))
		TravelDirection = Velocity.GetSafeNormal2D();
	ReleaseInvalidSlots(FrontSlots, true);
	ReleaseInvalidSlots(RearSlots, false);
}

bool ULSCastleEscortComponent::TryGetDestination(ALSTroopUnit* Troop, FVector& OutDestination)
{
	if (!ULSCombatTargetLibrary::IsCombatAlive(GetOwner()) ||
	    !ULSCombatTargetLibrary::IsCombatAlive(Troop) || Troop->GetFaction() != ELSFaction::Ally ||
	    Troop->GetWorld() != GetWorld() || Columns < 1 || !FMath::IsFinite(SlotSpacing) || SlotSpacing <= 0 ||
	    !FMath::IsFinite(CastleClearance) || CastleClearance < 0)
		return false;
	const bool bFront = FrontlineTroopTypes.Contains(Troop->GetTroopType());
	auto& Slots = bFront ? FrontSlots : RearSlots;
	ReleaseInvalidSlots(Slots, bFront);
	int32 SlotIndex = Slots.IndexOfByPredicate([Troop](const auto& Slot) { return Slot.Get() == Troop; });
	if (SlotIndex == INDEX_NONE)
	{
		SlotIndex = Slots.IndexOfByPredicate([](const auto& Slot) { return !Slot.IsValid(); });
		if (SlotIndex == INDEX_NONE)
			SlotIndex = Slots.Add(Troop);
		else
			Slots[SlotIndex] = Troop;
	}
	float CastleRadius, CastleHeight, TroopRadius, TroopHeight;
	GetOwner()->GetSimpleCollisionCylinder(CastleRadius, CastleHeight);
	Troop->GetSimpleCollisionCylinder(TroopRadius, TroopHeight);
	const int32 Column = SlotIndex % Columns;
	const int32 SideIndex = (Column + 1) / 2 * (Column % 2 == 0 ? 1 : -1);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, TravelDirection);
	const float ForwardDistance = CastleRadius + TroopRadius + CastleClearance +
	    (SlotIndex / Columns) * SlotSpacing;
	OutDestination = GetOwner()->GetActorLocation() - FVector(0, 0, CastleHeight) +
	    TravelDirection * ForwardDistance * (bFront ? 1 : -1) + Right * SideIndex * SlotSpacing;
	return true;
}
