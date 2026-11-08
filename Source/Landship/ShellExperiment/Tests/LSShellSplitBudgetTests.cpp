#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellSplitBudget.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitScalarTest, "Landship.ShellExperiment.SplitBudget.ScalarAndDirectConservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitScalarTest::RunTest(const FString& Parameters)
{
	for (ELSShellSplitOrder Order : { ELSShellSplitOrder::SplitFirst, ELSShellSplitOrder::SplitSecond })
	{
		const auto Allocation = FLSShellSplitBudget::Allocate(ELSShellModule::Blast, Order);
		TestTrue(TEXT("Supported composition has a budget"), Allocation.bValid);
		float TotalEffect = Allocation.Parent.EffectScale;
		float TotalDirect = Allocation.Parent.DirectDamage;
		for (const auto& Child : Allocation.Children)
		{
			TotalEffect += Child.EffectScale;
			TotalDirect += Child.DirectDamage;
		}
		TestTrue(TEXT("Parent and children share one module budget"), FMath::IsNearlyEqual(TotalEffect, 1.f, KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Parent and children share one50damage direct budget"), FMath::IsNearlyEqual(TotalDirect, 50.f, KINDA_SMALL_NUMBER));
		TestEqual(TEXT("Split-first parent has no direct hit; Split-second parent receives half"),
			Allocation.Parent.DirectDamage, Order == ELSShellSplitOrder::SplitFirst ? 0.f : 25.f);
		const auto Thrust = FLSShellSplitBudget::Allocate(ELSShellModule::Thrust, Order);
		TestEqual(TEXT("Original Thrust-first direct hit is harmless"), Thrust.Parent.DirectDamage, 0.f);
		for (const auto& Child : Thrust.Children) TestEqual(TEXT("Inherited Thrust direct hit is harmless"), Child.DirectDamage, 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitRemainingTest, "Landship.ShellExperiment.SplitBudget.RemainingResourcesDoNotReserveAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitRemainingTest::RunTest(const FString& Parameters)
{
	for (ELSShellModule Module : { ELSShellModule::Electric, ELSShellModule::Gravity, ELSShellModule::Iron })
	{
		const auto Transfer = FLSShellSplitBudget::Allocate(Module, ELSShellSplitOrder::SplitSecond);
		TestEqual(TEXT("Existing resource starts with one full parent budget"), Transfer.Parent.EffectScale, 1.f);
		for (const auto& Child : Transfer.Children) TestEqual(TEXT("Remaining-resource child cannot reserve a new scalar budget"), Child.EffectScale, 0.f);
		TestTrue(TEXT("Only Electric exports remaining charge"), Transfer.bExportRemainingCharge == (Module == ELSShellModule::Electric));
		TestTrue(TEXT("Only Gravity exports remaining field budget"), Transfer.bExportRemainingGravity == (Module == ELSShellModule::Gravity));
		TestTrue(TEXT("Only Iron replaces existing generated mass"), Transfer.bSplitExistingGeneratedMass == (Module == ELSShellModule::Iron));
		const auto Fresh = FLSShellSplitBudget::Allocate(Module, ELSShellSplitOrder::SplitFirst);
		TestFalse(TEXT("Split-first does not withdraw existing resources"), Fresh.bExportRemainingCharge || Fresh.bExportRemainingGravity || Fresh.bSplitExistingGeneratedMass);
		for (const auto& Child : Fresh.Children) TestTrue(TEXT("Fresh child receives one third"), FMath::IsNearlyEqual(Child.EffectScale, 1.f / 3));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitMaterialTest, "Landship.ShellExperiment.SplitBudget.AdhesionSlotsAndColdTimeConserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitMaterialTest::RunTest(const FString& Parameters)
{
	const auto SplitFirst = FLSShellSplitBudget::Allocate(ELSShellModule::Adhesion, ELSShellSplitOrder::SplitFirst);
	TestEqual(TEXT("Split-first parent has no bonds"), SplitFirst.Parent.AdhesionSlots, 0);
	for (const auto& Child : SplitFirst.Children) TestEqual(TEXT("Three full bond slots give one per child"), Child.AdhesionSlots, 1);
	const auto GlueFirst = FLSShellSplitBudget::Allocate(ELSShellModule::Adhesion, ELSShellSplitOrder::SplitSecond);
	TestEqual(TEXT("Glue-first parent keeps one realized slot"), GlueFirst.Parent.AdhesionSlots, 1);
	TestEqual(TEXT("First child receives first remainder"), GlueFirst.Children[0].AdhesionSlots, 1);
	TestEqual(TEXT("Second child receives second remainder"), GlueFirst.Children[1].AdhesionSlots, 1);
	TestEqual(TEXT("Third child cannot duplicate a fourth bond"), GlueFirst.Children[2].AdhesionSlots, 0);
	for (ELSShellSplitOrder Order : { ELSShellSplitOrder::SplitFirst, ELSShellSplitOrder::SplitSecond })
	{
		const auto Cold = FLSShellSplitBudget::Allocate(ELSShellModule::Cooling, Order);
		float TotalSeconds = Cold.Parent.ColdSeconds;
		for (const auto& Child : Cold.Children) TotalSeconds += Child.ColdSeconds;
		TestTrue(TEXT("Parent and children share four seconds rather than four per child"), FMath::IsNearlyEqual(TotalSeconds, 4.f, KINDA_SMALL_NUMBER));
		TestEqual(TEXT("Cooling-first target uses half the duration"), Cold.Parent.ColdSeconds,
			Order == ELSShellSplitOrder::SplitFirst ? 0.f : 2.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitChargeLineageTest, "Landship.ShellExperiment.SplitBudget.ChargeFragmentsKeepLineageAndTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitChargeLineageTest::RunTest(const FString& Parameters)
{
	FLSShellChargePacket Remaining;
	Remaining.PacketId = 10;
	Remaining.ParentPacketId = 5;
	Remaining.Sequence = 5;
	Remaining.SourceId = 2;
	Remaining.ShotId = 3;
	Remaining.CarrierBodyId = 4;
	Remaining.ActivationSequence = 6;
	Remaining.Source = GetMutableDefault<AActor>();
	Remaining.Faction = ELSFaction::Enemy;
	Remaining.Provenance = ELSShellChargeProvenance::Player;
	Remaining.Amount = 100;
	uint64 NextPacketId = 20;
	const auto Fragments = FLSShellSplitBudget::FragmentChargePacket(Remaining, [&NextPacketId]() { return NextPacketId++; });
	if (!TestEqual(TEXT("Remaining packet produces exactly three fragments"), Fragments.Num(), 3)) return false;
	FLSShellChargeStore SharedRecipient;
	float Total = 0;
	for (int32 Index = 0; Index < Fragments.Num(); ++Index)
	{
		const auto& Fragment = Fragments[Index];
		TestEqual(TEXT("Each child has its own global packet identity"), Fragment.PacketId, uint64(20 + Index));
		TestEqual(TEXT("Immediate original packet is the lineage parent"), Fragment.ParentPacketId, Remaining.PacketId);
		TestEqual(TEXT("Lineage age survives fragmentation"), Fragment.Sequence, Remaining.Sequence);
		TestEqual(TEXT("Source identity survives fragmentation"), Fragment.SourceId, Remaining.SourceId);
		TestEqual(TEXT("Shot identity survives fragmentation"), Fragment.ShotId, Remaining.ShotId);
		TestEqual(TEXT("Carrier snapshot is preserved until actual child storage"), Fragment.CarrierBodyId, Remaining.CarrierBodyId);
		TestEqual(TEXT("Activation snapshot is preserved until actual child storage"), Fragment.ActivationSequence, Remaining.ActivationSequence);
		TestTrue(TEXT("Weak source, faction and provenance survive"), Fragment.Source == Remaining.Source && Fragment.Faction == Remaining.Faction && Fragment.Provenance == Remaining.Provenance);
		Total += Fragment.Amount;
		TestEqual(TEXT("Distinct fragments can all reach the same body"), SharedRecipient.Add(Fragment, 360, 99), Fragment.Amount);
	}
	TestTrue(TEXT("Only remaining charge is distributed"), FMath::IsNearlyEqual(Total, Remaining.Amount, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Shared body receives every fragment without identity collision"), FMath::IsNearlyEqual(SharedRecipient.GetStoredCharge(), Remaining.Amount, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Read-only fragmentation leaves original snapshot untouched"), Remaining.Amount, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitInvalidTest, "Landship.ShellExperiment.SplitBudget.InvalidInputsHaveNoAllocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitInvalidTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Split cannot split itself"), FLSShellSplitBudget::Allocate(ELSShellModule::Split, ELSShellSplitOrder::SplitFirst).bValid);
	TestFalse(TEXT("Invalid cold duration cannot allocate"), FLSShellSplitBudget::Allocate(ELSShellModule::Cooling,
		ELSShellSplitOrder::SplitFirst, 50, 3, std::numeric_limits<float>::quiet_NaN()).bValid);
	TestFalse(TEXT("Negative slots cannot allocate"), FLSShellSplitBudget::Allocate(ELSShellModule::Adhesion,
		ELSShellSplitOrder::SplitFirst, 50, -1).bValid);
	FLSShellChargePacket Remaining;
	Remaining.PacketId = Remaining.SourceId = Remaining.Sequence = 1;
	Remaining.ActivationSequence = Remaining.Sequence;
	Remaining.Amount = 90;
	int32 AllocatorCalls = 0;
	const auto InvalidFragments = FLSShellSplitBudget::FragmentChargePacket(Remaining, [&AllocatorCalls]() { ++AllocatorCalls; return uint64(2); });
	TestTrue(TEXT("Duplicate allocated IDs never return a partially valid fragment set"), InvalidFragments.IsEmpty());
	TestEqual(TEXT("Allocator side effects are bounded to the rejected second ID"), AllocatorCalls, 2);
	TestEqual(TEXT("Rejected fragmentation does not consume the original"), Remaining.Amount, 90.f);
	return true;
}

#endif
