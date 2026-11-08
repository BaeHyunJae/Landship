#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellChargeStore.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
FLSShellChargePacket MakeChargePacket(uint64 PacketId, float Amount, ELSShellChargeProvenance Provenance = ELSShellChargeProvenance::Player)
{
	FLSShellChargePacket Packet;
	Packet.PacketId = PacketId;
	Packet.ParentPacketId = 50 + PacketId;
	Packet.Sequence = PacketId;
	Packet.SourceId = 100 + PacketId;
	Packet.ShotId = 200 + PacketId;
	Packet.CarrierBodyId = 300 + PacketId;
	Packet.ActivationSequence = 400 + PacketId;
	Packet.Source = GetMutableDefault<AActor>();
	Packet.Faction = ELSFaction::Enemy;
	Packet.Provenance = Provenance;
	Packet.Amount = Amount;
	return Packet;
}

void CheckChargeIdentity(FAutomationTestBase& Test, const FLSShellChargePacket& Actual, const FLSShellChargePacket& Expected)
{
	Test.TestEqual(TEXT("Packet identity survives transfer and consumption"), Actual.PacketId, Expected.PacketId);
	Test.TestEqual(TEXT("Parent identity survives"), Actual.ParentPacketId, Expected.ParentPacketId);
	Test.TestEqual(TEXT("Original charge age survives"), Actual.Sequence, Expected.Sequence);
	Test.TestEqual(TEXT("Original source identity survives"), Actual.SourceId, Expected.SourceId);
	Test.TestEqual(TEXT("Original shot identity survives"), Actual.ShotId, Expected.ShotId);
	Test.TestEqual(TEXT("Storage activation sequence survives"), Actual.ActivationSequence, Expected.ActivationSequence);
	Test.TestTrue(TEXT("Original weak source survives"), Actual.Source == Expected.Source);
	Test.TestTrue(TEXT("Original faction survives"), Actual.Faction == Expected.Faction);
	Test.TestTrue(TEXT("Original provenance survives"), Actual.Provenance == Expected.Provenance);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeStoreConservationTest, "Landship.ShellExperiment.ChargeStore.ConservationAndPartialConsumption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeStoreConservationTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	const FLSShellChargePacket First = MakeChargePacket(1, 35);
	const FLSShellChargePacket Second = MakeChargePacket(2, 15, ELSShellChargeProvenance::Environment);
	const float FirstAccepted = Store.Add(First, 100, 10);
	const float Accepted = FirstAccepted + Store.Add(Second, 100, 10);
	const FLSShellChargePacket Partial = Store.ConsumePacket(First.PacketId, 12);
	TestEqual(TEXT("Requested portion is returned"), Partial.Amount, 12.0f);
	CheckChargeIdentity(*this, Partial, First);
	TestEqual(TEXT("Consumption retains the actual carrier"), Partial.CarrierBodyId, uint64(10));
	if (!TestEqual(TEXT("Partial consumption retains both packets"), Store.GetPackets().Num(), 2)) return false;
	TestEqual(TEXT("Partial remainder stays first"), Store.GetPackets()[0].PacketId, First.PacketId);
	TestEqual(TEXT("Partial remainder is stored"), Store.GetPackets()[0].Amount, 23.0f);
	TestEqual(TEXT("Later packet stays second"), Store.GetPackets()[1].PacketId, Second.PacketId);
	CheckChargeIdentity(*this, Store.GetPackets()[0], First);
	const FLSShellChargePacket Rest = Store.ConsumePacket(First.PacketId, 100);
	const FLSShellChargePacket Missing = Store.ConsumePacket(First.PacketId, 100);
	TestEqual(TEXT("Over-request drains only the remainder"), Rest.Amount, 23.0f);
	TestEqual(TEXT("A consumed packet cannot supply charge twice"), Missing.Amount, 0.0f);
	TestEqual(TEXT("Accepted charge equals drained plus remaining charge"), Partial.Amount + Rest.Amount + Store.GetStoredCharge(), Accepted);
	if (!TestEqual(TEXT("Empty packet is removed"), Store.GetPackets().Num(), 1)) return false;
	CheckChargeIdentity(*this, Store.GetPackets()[0], Second);
	CheckChargeIdentity(*this, Store.ConsumePacket(Second.PacketId, 15), Second);
	TestTrue(TEXT("All accepted charge can be consumed"), Store.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeStoreOverflowTest, "Landship.ShellExperiment.ChargeStore.OverflowPreservesOlderPackets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeStoreOverflowTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	const FLSShellChargePacket Old = MakeChargePacket(1, 70);
	const FLSShellChargePacket New = MakeChargePacket(2, 50, ELSShellChargeProvenance::Environment);
	TestEqual(TEXT("First deposit accepted"), Store.Add(Old, 100, 10), 70.0f);
	TestEqual(TEXT("Only remaining capacity accepted"), Store.Add(New, 100, 20), 30.0f);
	TestEqual(TEXT("Full capacity rejects more charge"), Store.Add(MakeChargePacket(3, 40), 100, 30), 0.0f);
	if (!TestEqual(TEXT("Rejected overflow creates no packet"), Store.GetPackets().Num(), 2)) return false;
	CheckChargeIdentity(*this, Store.GetPackets()[0], Old);
	TestEqual(TEXT("Old amount survives overflow"), Store.GetPackets()[0].Amount, 70.0f);
	TestEqual(TEXT("Old carrier survives overflow"), Store.GetPackets()[0].CarrierBodyId, uint64(10));
	CheckChargeIdentity(*this, Store.GetPackets()[1], New);
	TestEqual(TEXT("Only new amount is clipped"), Store.GetPackets()[1].Amount, 30.0f);
	TestEqual(TEXT("New carrier is assigned"), Store.GetPackets()[1].CarrierBodyId, uint64(20));
	TestEqual(TEXT("Lower capacity does not evict existing charge"), Store.Add(MakeChargePacket(4, 40), 20, 40), 0.0f);
	TestEqual(TEXT("Stored charge remains at previous capacity"), Store.GetStoredCharge(), 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeStoreInvalidInputTest, "Landship.ShellExperiment.ChargeStore.InvalidInputsAndDuplicateIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeStoreInvalidInputTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	const FLSShellChargePacket Original = MakeChargePacket(1, 25);
	Store.Add(Original, 100, 10);
	const float Invalid[] = { 0, -1, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
	for (float Value : Invalid)
	{
		TestEqual(TEXT("Invalid deposit cannot add charge"), Store.Add(MakeChargePacket(2, Value), 100, 20), 0.0f);
		TestEqual(TEXT("Invalid capacity cannot add charge"), Store.Add(MakeChargePacket(2, 50), Value, 20), 0.0f);
		const FLSShellChargePacket Consumed = Store.ConsumePacket(Original.PacketId, Value);
		TestEqual(TEXT("Invalid drain cannot produce charge"), Consumed.Amount, 0.0f);
		CheckChargeIdentity(*this, Consumed, Original);
	}
	FLSShellChargePacket MissingPacketId = MakeChargePacket(2, 50);
	MissingPacketId.PacketId = 0;
	TestEqual(TEXT("Missing packet identity cannot add charge"), Store.Add(MissingPacketId, 100, 20), 0.0f);
	FLSShellChargePacket MissingSourceId = MakeChargePacket(2, 50, ELSShellChargeProvenance::Environment);
	MissingSourceId.SourceId = 0;
	TestEqual(TEXT("Environmental charge also requires source identity"), Store.Add(MissingSourceId, 100, 20), 0.0f);
	FLSShellChargePacket MissingSequence = MakeChargePacket(2, 50);
	MissingSequence.Sequence = 0;
	TestEqual(TEXT("Missing original charge age cannot add charge"), Store.Add(MissingSequence, 100, 20), 0.0f);
	FLSShellChargePacket MissingActivation = MakeChargePacket(2, 50);
	MissingActivation.ActivationSequence = 0;
	TestEqual(TEXT("Missing storage activation sequence cannot add charge"), Store.Add(MissingActivation, 100, 20), 0.0f);
	FLSShellChargePacket Duplicate = MakeChargePacket(Original.PacketId, 50, ELSShellChargeProvenance::Environment);
	Duplicate.SourceId = 999;
	TestEqual(TEXT("Duplicate identity cannot add energy or replace attribution"), Store.Add(Duplicate, 100, 20), 0.0f);
	TestEqual(TEXT("Invalid operations preserve stored charge"), Store.GetStoredCharge(), 25.0f);
	if (!TestEqual(TEXT("Invalid operations create no packets"), Store.GetPackets().Num(), 1)) return false;
	CheckChargeIdentity(*this, Store.GetPackets()[0], Original);
	TestEqual(TEXT("Invalid operations preserve carrier"), Store.GetPackets()[0].CarrierBodyId, uint64(10));
	TestEqual(TEXT("Unknown identity supplies no charge"), Store.ConsumePacket(99, 10).Amount, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeStoreClearTest, "Landship.ShellExperiment.ChargeStore.Clear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeStoreClearTest::RunTest(const FString& Parameters)
{
	FLSShellChargeStore Store;
	Store.Add(MakeChargePacket(1, 25), 100, 10);
	Store.Clear();
	TestTrue(TEXT("Clear removes all packets"), Store.IsEmpty());
	TestEqual(TEXT("Clear removes all stored charge"), Store.GetStoredCharge(), 0.0f);
	TestEqual(TEXT("Cleared charge cannot be consumed"), Store.ConsumePacket(1, 25).Amount, 0.0f);
	TestEqual(TEXT("Cleared identity can carry a new deposit"), Store.Add(MakeChargePacket(1, 40), 100, 20), 40.0f);
	TestEqual(TEXT("New deposit is independent of cleared charge"), Store.GetStoredCharge(), 40.0f);
	return true;
}

#endif
