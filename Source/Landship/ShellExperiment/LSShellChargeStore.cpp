#include "LSShellChargeStore.h"

float FLSShellChargeStore::Add(const FLSShellChargePacket& Packet, float Capacity, uint64 CarrierBodyId)
{
	if (Packet.PacketId == 0 || Packet.SourceId == 0 || Packet.Sequence == 0 || Packet.ActivationSequence == 0
		|| !FMath::IsFinite(Packet.Amount) || Packet.Amount <= 0 || !FMath::IsFinite(Capacity) || Capacity <= 0
		|| Packets.ContainsByPredicate([&Packet](const FLSShellChargePacket& Stored) { return Stored.PacketId == Packet.PacketId; }))
	{
		return 0;
	}

	const float AcceptedAmount = FMath::Min(Packet.Amount, FMath::Max(0.0f, Capacity - GetStoredCharge()));
	if (AcceptedAmount <= 0) return 0;

	FLSShellChargePacket& Stored = Packets.Add_GetRef(Packet);
	Stored.Amount = AcceptedAmount;
	Stored.CarrierBodyId = CarrierBodyId;
	return AcceptedAmount;
}

float FLSShellChargeStore::GetStoredCharge() const
{
	double Total = 0;
	for (const FLSShellChargePacket& Packet : Packets) Total += Packet.Amount;
	return static_cast<float>(Total);
}

FLSShellChargePacket FLSShellChargeStore::ConsumePacket(uint64 PacketId, float RequestedAmount)
{
	const int32 Index = Packets.IndexOfByPredicate([PacketId](const FLSShellChargePacket& Packet) { return Packet.PacketId == PacketId; });
	FLSShellChargePacket Consumed;
	Consumed.PacketId = PacketId;
	if (Index == INDEX_NONE) return Consumed;

	FLSShellChargePacket& Stored = Packets[Index];
	Consumed = Stored;
	Consumed.Amount = 0;
	if (!FMath::IsFinite(RequestedAmount) || RequestedAmount <= 0) return Consumed;

	Consumed.Amount = FMath::Min(Stored.Amount, RequestedAmount);
	Stored.Amount -= Consumed.Amount;
	if (Stored.Amount <= 0) Packets.RemoveAt(Index);
	return Consumed;
}

void FLSShellChargeStore::Clear()
{
	Packets.Reset();
}
