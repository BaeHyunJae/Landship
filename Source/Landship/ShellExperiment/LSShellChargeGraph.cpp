#include "LSShellChargeGraph.h"
#include "Algo/Reverse.h"

namespace
{
using FLSChargeNodeMap = TMap<uint64, const FLSShellChargeGraphNode*>;

TSet<uint64> FindReachableChargeNodes(const FLSChargeNodeMap& Nodes, uint64 SourceBodyId, bool bExpandSource,
	TMap<uint64, uint64>* Predecessors = nullptr)
{
	TSet<uint64> Visited;
	TArray<uint64> Pending { SourceBodyId };
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		const uint64 BodyId = Pending[Index];
		const FLSShellChargeGraphNode* const* Found = Nodes.Find(BodyId);
		if (!Found || Visited.Contains(BodyId)) continue;
		Visited.Add(BodyId);
		const FLSShellChargeGraphNode& Node = **Found;
		if (!Node.bRelay && !(bExpandSource && BodyId == SourceBodyId)) continue;
		if (Predecessors)
		{
			for (uint64 NeighborId : Node.Connections)
			{
				if (Nodes.Contains(NeighborId) && !Visited.Contains(NeighborId) && !Predecessors->Contains(NeighborId))
				{
					Predecessors->Add(NeighborId, BodyId);
				}
			}
		}
		Pending.Append(Node.Connections);
	}
	return Visited;
}

struct FLSPendingChargePacket
{
	uint64 CarrierBodyId = 0;
	FLSShellChargeStore* Store = nullptr;
	FLSShellChargePacket Packet;
	TArray<uint64> Recipients;
	TMap<uint64, uint64> Predecessors;
};
}

TArray<FLSShellChargeShare> FLSShellChargeGraph::ConsumeContact(const TArray<FLSShellChargeGraphNode>& Nodes,
	uint64 A, uint64 B, float Budget, uint64 ContactSequence)
{
	TArray<FLSShellChargeShare> Shares;
	if (!FMath::IsFinite(Budget) || Budget <= 0 || A == B) return Shares;

	FLSChargeNodeMap NodesById;
	for (const FLSShellChargeGraphNode& Node : Nodes)
	{
		if (NodesById.Contains(Node.BodyId)) return Shares;
		NodesById.Add(Node.BodyId, &Node);
	}
	if (!NodesById.Contains(A) || !NodesById.Contains(B)) return Shares;

	TSet<uint64> CarrierIds = FindReachableChargeNodes(NodesById, A, false);
	CarrierIds.Append(FindReachableChargeNodes(NodesById, B, false));
	TArray<FLSPendingChargePacket> PendingPackets;
	for (uint64 CarrierBodyId : CarrierIds)
	{
		const FLSShellChargeGraphNode& Carrier = *NodesById[CarrierBodyId];
		if (!Carrier.Store || Carrier.Store->IsEmpty()) continue;

		TArray<uint64> Recipients;
		TMap<uint64, uint64> Predecessors;
		for (uint64 BodyId : FindReachableChargeNodes(NodesById, CarrierBodyId, true, &Predecessors))
		{
			if (BodyId != CarrierBodyId && NodesById[BodyId]->bDamageEndpoint) Recipients.Add(BodyId);
		}
		if (Recipients.IsEmpty()) continue;
		Recipients.Sort();
		for (const FLSShellChargePacket& Packet : Carrier.Store->GetPackets())
		{
			if (Packet.ActivationSequence >= ContactSequence) continue;
			PendingPackets.Add({ CarrierBodyId, Carrier.Store, Packet, Recipients, Predecessors });
		}
	}
	PendingPackets.Sort([](const FLSPendingChargePacket& Left, const FLSPendingChargePacket& Right)
	{
		if (Left.Packet.Sequence != Right.Packet.Sequence) return Left.Packet.Sequence < Right.Packet.Sequence;
		return Left.Packet.PacketId != Right.Packet.PacketId
			? Left.Packet.PacketId < Right.Packet.PacketId : Left.CarrierBodyId < Right.CarrierBodyId;
	});

	float RemainingBudget = Budget;
	for (const FLSPendingChargePacket& Pending : PendingPackets)
	{
		if (RemainingBudget <= 0) break;
		const FLSShellChargePacket Consumed = Pending.Store->ConsumePacket(Pending.Packet.PacketId,
			FMath::Min(RemainingBudget, Pending.Packet.Amount));
		if (Consumed.Amount <= 0) continue;
		RemainingBudget -= Consumed.Amount;
		const float PerRecipient = Consumed.Amount / Pending.Recipients.Num();
		for (uint64 RecipientBodyId : Pending.Recipients)
		{
			FLSShellChargeShare& Share = Shares.AddDefaulted_GetRef();
			Share.Packet = Consumed;
			Share.Packet.Amount = PerRecipient;
			Share.RecipientBodyId = RecipientBodyId;
			Share.RequestedDamage = PerRecipient;
			for (uint64 BodyId = RecipientBodyId; ; BodyId = Pending.Predecessors.FindChecked(BodyId))
			{
				Share.PathBodyIds.Add(BodyId);
				if (BodyId == Pending.CarrierBodyId) break;
			}
			Algo::Reverse(Share.PathBodyIds);
		}
	}
	return Shares;
}
