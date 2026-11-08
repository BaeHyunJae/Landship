#include "LSShellSplitBudget.h"

FLSShellSplitAllocation FLSShellSplitBudget::Allocate(ELSShellModule PairedModule, ELSShellSplitOrder Order,
	float OriginalDirectDamage, int32 AdhesionSlots, float ColdSeconds)
{
	FLSShellSplitAllocation Allocation;
	if (!FMath::IsFinite(OriginalDirectDamage) || OriginalDirectDamage < 0 || AdhesionSlots < 0
		|| !FMath::IsFinite(ColdSeconds) || ColdSeconds < 0
		|| (Order != ELSShellSplitOrder::SplitFirst && Order != ELSShellSplitOrder::SplitSecond)) return Allocation;
	switch (PairedModule)
	{
	case ELSShellModule::Blast:
	case ELSShellModule::Gravity:
	case ELSShellModule::Electric:
	case ELSShellModule::Thrust:
	case ELSShellModule::Iron:
	case ELSShellModule::Adhesion:
	case ELSShellModule::Cooling: break;
	default: return Allocation;
	}

	Allocation.bValid = true;
	const bool bSplitFirst = Order == ELSShellSplitOrder::SplitFirst;
	Allocation.Parent.EffectScale = bSplitFirst ? 0 : .5f;
	Allocation.Parent.DirectDamage = bSplitFirst || PairedModule == ELSShellModule::Thrust ? 0 : OriginalDirectDamage / 2;
	for (FLSShellSplitPortion& Child : Allocation.Children)
	{
		Child.EffectScale = bSplitFirst ? 1.f / 3 : 1.f / 6;
		Child.DirectDamage = PairedModule == ELSShellModule::Thrust ? 0 : OriginalDirectDamage / (bSplitFirst ? 3 : 6);
	}
	if (!bSplitFirst)
	{
		Allocation.bExportRemainingCharge = PairedModule == ELSShellModule::Electric;
		Allocation.bExportRemainingGravity = PairedModule == ELSShellModule::Gravity;
		Allocation.bSplitExistingGeneratedMass = PairedModule == ELSShellModule::Iron;
		if (Allocation.bExportRemainingCharge || Allocation.bExportRemainingGravity || Allocation.bSplitExistingGeneratedMass)
		{
			Allocation.Parent.EffectScale = 1;
			for (FLSShellSplitPortion& Child : Allocation.Children) Child.EffectScale = 0;
		}
	}
	if (PairedModule == ELSShellModule::Adhesion)
	{
		Allocation.Parent.AdhesionSlots = bSplitFirst ? 0 : AdhesionSlots / 2;
		const int32 ChildSlots = AdhesionSlots - Allocation.Parent.AdhesionSlots;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Allocation.Children[Index].AdhesionSlots = ChildSlots / 3 + (Index < ChildSlots % 3 ? 1 : 0);
		}
	}
	if (PairedModule == ELSShellModule::Cooling)
	{
		Allocation.Parent.ColdSeconds = ColdSeconds * Allocation.Parent.EffectScale;
		for (FLSShellSplitPortion& Child : Allocation.Children) Child.ColdSeconds = ColdSeconds * Child.EffectScale;
	}
	return Allocation;
}

TArray<FLSShellChargePacket> FLSShellSplitBudget::FragmentChargePacket(const FLSShellChargePacket& RemainingPacket,
	TFunctionRef<uint64()> AllocatePacketId)
{
	TArray<FLSShellChargePacket> Fragments;
	if (!RemainingPacket.PacketId || !RemainingPacket.SourceId || !RemainingPacket.Sequence
		|| !FMath::IsFinite(RemainingPacket.Amount) || RemainingPacket.Amount <= 0) return Fragments;
	const float PerChild = RemainingPacket.Amount / 3;
	if (PerChild <= 0) return Fragments;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const uint64 PacketId = AllocatePacketId();
		if (!PacketId || PacketId == RemainingPacket.PacketId || Fragments.ContainsByPredicate(
			[PacketId](const FLSShellChargePacket& Packet) { return Packet.PacketId == PacketId; })) return {};
		FLSShellChargePacket& Fragment = Fragments.Add_GetRef(RemainingPacket);
		Fragment.PacketId = PacketId;
		Fragment.ParentPacketId = RemainingPacket.PacketId;
		Fragment.Amount = Index < 2 ? PerChild : RemainingPacket.Amount - PerChild * 2;
	}
	return Fragments;
}
