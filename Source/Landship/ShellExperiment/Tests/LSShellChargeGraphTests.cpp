#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellChargeGraph.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
FLSShellChargeGraphNode MakeChargeNode(uint64 BodyId, bool bRelay, bool bDamageEndpoint,
	FLSShellChargeStore* Store, TArray<uint64> Connections)
{
	return { BodyId, bRelay, bDamageEndpoint, Store, MoveTemp(Connections) };
}

void StoreCharge(FLSShellChargeStore& Store, uint64 PacketId, uint64 BodyId, float Amount,
	ELSShellChargeProvenance Provenance = ELSShellChargeProvenance::Player)
{
	FLSShellChargePacket Packet;
	Packet.PacketId = PacketId;
	Packet.Sequence = PacketId;
	Packet.ActivationSequence = Packet.Sequence;
	Packet.SourceId = 100 + PacketId;
	Packet.ShotId = 200 + PacketId;
	Packet.Faction = ELSFaction::Enemy;
	Packet.Provenance = Provenance;
	Packet.Amount = Amount;
	Store.Add(Packet, 1000, BodyId);
}

float SumRequestedChargeDamage(const TArray<FLSShellChargeShare>& Shares)
{
	float Total = 0;
	for (const FLSShellChargeShare& Share : Shares) Total += Share.RequestedDamage;
	return Total;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphCyclesTest, "Landship.ShellExperiment.ChargeGraph.CyclesHaveUniqueRecipients",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphCyclesTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	StoreCharge(Store, 1, 1, 90);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, true, &Store, { 2, 3 }),
		MakeChargeNode(2, true, false, nullptr, { 1, 3, 4, 4 }),
		MakeChargeNode(3, true, false, nullptr, { 1, 2, 5 }),
		MakeChargeNode(4, false, true, nullptr, { 2 }),
		MakeChargeNode(5, false, true, nullptr, { 3 })
	};
	const TArray<FLSShellChargeShare> Shares = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90);
	if (!TestEqual(TEXT("Cyclic paths and duplicate edges do not duplicate recipients"), Shares.Num(), 2)) return false;
	TestEqual(TEXT("First unique recipient"), Shares[0].RecipientBodyId, uint64(4));
	TestEqual(TEXT("Second unique recipient"), Shares[1].RecipientBodyId, uint64(5));
	TestEqual(TEXT("First recipient receives half"), Shares[0].RequestedDamage, 45.0f);
	TestEqual(TEXT("Second recipient receives half"), Shares[1].RequestedDamage, 45.0f);
	TestEqual(TEXT("Share packet carries only its recipient amount"), Shares[0].Packet.Amount, 45.0f);
	TestEqual(TEXT("All requested damage is backed by consumed charge"), SumRequestedChargeDamage(Shares), 90.0f);
	bool bPathsFollowActualRelayConnections = true;
	for (const FLSShellChargeShare& Share : Shares)
	{
		if (Share.PathBodyIds.Num() < 2 || Share.PathBodyIds[0] != 1 || Share.PathBodyIds.Last() != Share.RecipientBodyId)
		{
			bPathsFollowActualRelayConnections = false;
			continue;
		}
		for (int32 Index = 1; Index < Share.PathBodyIds.Num(); ++Index)
		{
			const uint64 PreviousId = Share.PathBodyIds[Index - 1];
			const FLSShellChargeGraphNode* Previous = Nodes.FindByPredicate(
				[PreviousId](const FLSShellChargeGraphNode& Node) { return Node.BodyId == PreviousId; });
			bPathsFollowActualRelayConnections &= Previous && Previous->Connections.Contains(Share.PathBodyIds[Index])
				&& (Index == 1 || Previous->bRelay);
		}
	}
	TestTrue(TEXT("Every returned path follows declared edges through relay intermediates"), bPathsFollowActualRelayConnections);
	TestTrue(TEXT("Charge was consumed once"), Store.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphTerminalTest, "Landship.ShellExperiment.ChargeGraph.NonRelayEndpointsDoNotBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphTerminalTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore NearStore;
	FLSShellChargeStore FarStore;
	StoreCharge(NearStore, 2, 1, 90);
	StoreCharge(FarStore, 1, 5, 90);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, false, true, &NearStore, { 2 }),
		MakeChargeNode(2, true, false, nullptr, { 1, 3 }),
		MakeChargeNode(3, false, true, nullptr, { 2, 4 }),
		MakeChargeNode(4, true, false, nullptr, { 3, 5 }),
		MakeChargeNode(5, false, true, &FarStore, { 4 })
	};
	const TArray<FLSShellChargeShare> Shares = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90);
	if (!TestEqual(TEXT("Terminal troop source can discharge through adjacent relay"), Shares.Num(), 1)) return false;
	TestEqual(TEXT("Adjacent terminal receives damage"), Shares[0].RecipientBodyId, uint64(3));
	TestEqual(TEXT("An older packet beyond the terminal cannot join the contact"), Shares[0].Packet.PacketId, uint64(2));
	TestTrue(TEXT("Near source was consumed"), NearStore.IsEmpty());
	TestEqual(TEXT("Terminal cannot bridge to another component's storage"), FarStore.GetStoredCharge(), 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphNoRecipientTest, "Landship.ShellExperiment.ChargeGraph.NoRecipientPreservesCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphNoRecipientTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	StoreCharge(Store, 1, 1, 90);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, true, &Store, { 2 }),
		MakeChargeNode(2, true, false, nullptr, { 1, 999 }),
		MakeChargeNode(3, false, true, nullptr, {})
	};
	TestTrue(TEXT("Carrier itself and disconnected endpoints cannot receive discharge"),
		FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90).IsEmpty());
	TestEqual(TEXT("No valid recipient means no consumption"), Store.GetStoredCharge(), 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphContactBudgetTest, "Landship.ShellExperiment.ChargeGraph.DistinctContactsConsumeSeparateBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphContactBudgetTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	StoreCharge(Store, 1, 1, 250);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, false, &Store, { 2, 3 }),
		MakeChargeNode(2, false, true, nullptr, { 1 }),
		MakeChargeNode(3, false, true, nullptr, { 1 })
	};
	const auto First = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90);
	const auto Second = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 3, 90);
	TestEqual(TEXT("First legitimate contact spends one budget"), SumRequestedChargeDamage(First), 90.0f);
	TestEqual(TEXT("Second legitimate contact spends another budget"), SumRequestedChargeDamage(Second), 90.0f);
	TestEqual(TEXT("Two events consume 180 rather than cloning the packet"), Store.GetStoredCharge(), 70.0f);
	TestEqual(TEXT("Requested damage plus remaining charge equals original charge"),
		SumRequestedChargeDamage(First) + SumRequestedChargeDamage(Second) + Store.GetStoredCharge(), 250.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphPacketOrderTest, "Landship.ShellExperiment.ChargeGraph.OlderPacketsUseGlobalBudgetAndKeepProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphPacketOrderTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore NewStore;
	FLSShellChargeStore OldStore;
	StoreCharge(NewStore, 20, 1, 80);
	FLSShellChargePacket OlderChild;
	OlderChild.PacketId = 30;
	OlderChild.ParentPacketId = 10;
	OlderChild.Sequence = 10;
	OlderChild.SourceId = 110;
	OlderChild.ShotId = 210;
	OlderChild.ActivationSequence = 4;
	OlderChild.Faction = ELSFaction::Enemy;
	OlderChild.Provenance = ELSShellChargeProvenance::Environment;
	OlderChild.Amount = 30;
	TestEqual(TEXT("First lineage child is accepted"), OldStore.Add(OlderChild, 1000, 2), 30.0f);
	OlderChild.PacketId = 31;
	TestEqual(TEXT("A different child identity can share the same carrier"), OldStore.Add(OlderChild, 1000, 2), 30.0f);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, false, &NewStore, { 2, 3 }),
		MakeChargeNode(2, true, false, &OldStore, { 1, 3 }),
		MakeChargeNode(3, false, true, nullptr, { 1, 2 })
	};
	const auto Shares = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90);
	if (!TestEqual(TEXT("Lineage children and new packet remain separate"), Shares.Num(), 3)) return false;
	TestEqual(TEXT("Later child ID with older charge age is consumed first"), Shares[0].Packet.PacketId, uint64(30));
	TestEqual(TEXT("Second child of the same age follows packet identity order"), Shares[1].Packet.PacketId, uint64(31));
	TestEqual(TEXT("Old lineage receives first share of global budget"), Shares[0].RequestedDamage + Shares[1].RequestedDamage, 60.0f);
	TestEqual(TEXT("Original lineage identity remains"), Shares[0].Packet.ParentPacketId, uint64(10));
	TestEqual(TEXT("Original lineage age remains"), Shares[0].Packet.Sequence, uint64(10));
	TestEqual(TEXT("Storage activation remains independent of lineage age"), Shares[0].Packet.ActivationSequence, uint64(4));
	TestTrue(TEXT("Environmental provenance remains environmental"), Shares[0].Packet.Provenance == ELSShellChargeProvenance::Environment);
	TestEqual(TEXT("Original environmental source remains"), Shares[0].Packet.SourceId, uint64(110));
	TestEqual(TEXT("Original environmental shot remains"), Shares[0].Packet.ShotId, uint64(210));
	TestEqual(TEXT("Original carrier remains"), Shares[0].Packet.CarrierBodyId, uint64(2));
	TestTrue(TEXT("Original faction remains"), Shares[0].Packet.Faction == ELSFaction::Enemy);
	TestEqual(TEXT("New packet follows older lineage"), Shares[2].Packet.PacketId, uint64(20));
	TestEqual(TEXT("New packet receives only remaining budget"), Shares[2].RequestedDamage, 30.0f);
	TestTrue(TEXT("Player provenance remains separate"), Shares[2].Packet.Provenance == ELSShellChargeProvenance::Player);
	TestTrue(TEXT("Old packet drained"), OldStore.IsEmpty());
	TestEqual(TEXT("New packet retains unused charge"), NewStore.GetStoredCharge(), 50.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphActivationOrderTest, "Landship.ShellExperiment.ChargeGraph.ContactBeforeActivationDoesNotDischarge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphActivationOrderTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	FLSShellChargePacket Packet;
	Packet.PacketId = 1;
	Packet.SourceId = 101;
	Packet.Sequence = 1;
	Packet.Amount = 180;
	Packet.ActivationSequence = 2;
	Store.Add(Packet, 1000, 1);
	const TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, false, &Store, { 2 }),
		MakeChargeNode(2, false, true, nullptr, { 1 })
	};
	TestTrue(TEXT("Queued contact that happened before activation cannot discharge later stored charge"),
		FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90, 1).IsEmpty());
	TestTrue(TEXT("Activation must strictly precede the contact"),
		FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90, 2).IsEmpty());
	TestEqual(TEXT("Earlier contact preserves all charge"), Store.GetStoredCharge(), 180.0f);
	const auto LaterContact = FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90, 3);
	TestEqual(TEXT("Contact after activation consumes one budget"), SumRequestedChargeDamage(LaterContact), 90.0f);
	TestEqual(TEXT("Only the later contact reduces stored charge"), Store.GetStoredCharge(), 90.0f);
	if (!TestEqual(TEXT("Later contact returns one recipient"), LaterContact.Num(), 1)) return false;
	TestEqual(TEXT("Share preserves original activation sequence"), LaterContact[0].Packet.ActivationSequence, uint64(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeGraphInvalidTest, "Landship.ShellExperiment.ChargeGraph.InvalidGraphAndBudgetDoNotConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeGraphInvalidTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	StoreCharge(Store, 1, 1, 90);
	TArray<FLSShellChargeGraphNode> Nodes {
		MakeChargeNode(1, true, false, &Store, { 2 }),
		MakeChargeNode(2, false, true, nullptr, { 1 })
	};
	const float Invalid[] { 0, -1, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
	for (float Budget : Invalid)
	{
		TestTrue(TEXT("Invalid budget creates no shares"), FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, Budget).IsEmpty());
	}
	TestTrue(TEXT("Missing contact endpoint creates no shares"), FLSShellChargeGraph::ConsumeContact(Nodes, 1, 99, 90).IsEmpty());
	TestTrue(TEXT("Self contact creates no shares"), FLSShellChargeGraph::ConsumeContact(Nodes, 1, 1, 90).IsEmpty());
	Nodes.Add(MakeChargeNode(1, false, true, nullptr, {}));
	TestTrue(TEXT("Duplicate body identity rejects the whole graph"), FLSShellChargeGraph::ConsumeContact(Nodes, 1, 2, 90).IsEmpty());
	TestEqual(TEXT("All rejected operations preserve charge"), Store.GetStoredCharge(), 90.0f);
	return true;
}

#endif
