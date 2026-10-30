#if WITH_DEV_AUTOMATION_TESTS

#include "LSCombatStats.h"
#include "Misc/AssertionMacros.h"
#include "Misc/AutomationTest.h"
#include "Tests/EnsureScope.h"
#include "UObject/UnrealType.h"

#include <limits>

namespace
{
	constexpr const TCHAR* FailureMessage = TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed:");

	bool HandleExpectedStatsEnsure(FAutomationTestBase& Test, const FEnsureHandlerArgs& Args)
	{
		if (!FString(Args.Message).StartsWith(FailureMessage))
		{
			return false;
		}
		// ensure 본문은 Verbose 로그이므로, 실제 호출을 예상 오류로 전달해 횟수를 검증한다.
		Test.AddError(Args.Message);
		return true;
	}

	template <typename RowT>
	UDataTable* MakeTable(const RowT& Row)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = RowT::StaticStruct();
		Table->AddRow(TEXT("PrototypeTest"), Row);
		return Table;
	}

	FDataTableRowHandle MakeHandle(UDataTable* Table, FName RowName = TEXT("PrototypeTest"))
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = Table;
		Handle.RowName = RowName;
		return Handle;
	}

	template <typename RowT>
	RowT MakeValidRow()
	{
		RowT Row;
		float Value = 10.0f;
		for (TFieldIterator<FFloatProperty> It(RowT::StaticStruct()); It; ++It)
		{
			It->SetPropertyValue_InContainer(&Row, Value);
			Value += 10.0f;
		}
		return Row;
	}

	template <typename RowT>
	void CheckValidSchema(FAutomationTestBase& Test)
	{
		const RowT Source = MakeValidRow<RowT>();
		UDataTable* Table = MakeTable(Source);
		const FDataTableRowHandle Handle = MakeHandle(Table);
		RowT Output;
		Test.TestTrue(TEXT("Valid row succeeds"), LSCombatStats::TryReadRow(Handle, Table, Output) == ELSCombatStatsInitResult::Success);
		Test.TestTrue(TEXT("Every field is copied"), RowT::StaticStruct()->CompareScriptStruct(&Output, &Source, 0));

		RowT Changed = Source;
		Changed.Health += 5.0f;
		Table->AddRow(Handle.RowName, Changed);
		Test.TestEqual(TEXT("Output owns a copy after the table changes"), Output.Health, Source.Health);
		Test.TestTrue(TEXT("Next read succeeds"), LSCombatStats::TryReadRow(Handle, Table, Output) == ELSCombatStatsInitResult::Success);
		Test.TestEqual(TEXT("Next read observes the changed row"), Output.Health, Changed.Health);

		RowT ZeroOptional;
		ZeroOptional.Health = 1.0f;
		if (FFloatProperty* Interval = FindFProperty<FFloatProperty>(RowT::StaticStruct(), TEXT("AttackInterval")))
		{
			Interval->SetPropertyValue_InContainer(&ZeroOptional, 1.0f);
		}
		Table->AddRow(Handle.RowName, ZeroOptional);
		Test.TestTrue(TEXT("Optional zero values are allowed"), LSCombatStats::TryReadRow(Handle, Table, Output) == ELSCombatStatsInitResult::Success);
		Test.TestTrue(TEXT("Optional zero values are copied"), RowT::StaticStruct()->CompareScriptStruct(&Output, &ZeroOptional, 0));
	}

	template <typename RowT>
	void CheckInvalidValues(FAutomationTestBase& Test)
	{
		const RowT Source = MakeValidRow<RowT>();
		UDataTable* Table = MakeTable(Source);
		const FDataTableRowHandle Handle = MakeHandle(Table);
		RowT Output = Source;
		Output.Health = 987.0f;
		const RowT Sentinel = Output;
		const float InvalidValues[] = { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };

		for (TFieldIterator<FFloatProperty> It(RowT::StaticStruct()); It; ++It)
		{
			const auto CheckValue = [&](float Value)
			{
				RowT InvalidRow = Source;
				It->SetPropertyValue_InContainer(&InvalidRow, Value);
				Table->AddRow(Handle.RowName, InvalidRow);
				Test.TestTrue(*FString::Printf(TEXT("%s rejects invalid value"), *It->GetName()),
					LSCombatStats::TryReadRow(Handle, Table, Output) == ELSCombatStatsInitResult::InvalidValue);
				Test.TestTrue(TEXT("Invalid row preserves every output field"), RowT::StaticStruct()->CompareScriptStruct(&Output, &Sentinel, 0));
			};
			for (float Value : InvalidValues)
			{
				CheckValue(Value);
			}
			if (It->GetFName() == TEXT("Health") || It->GetFName() == TEXT("AttackInterval"))
			{
				CheckValue(0.0f);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatStatsValidSchemasTest, "Landship.Combat.Stats.ValidSchemas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatStatsValidSchemasTest::RunTest(const FString& Parameters)
{
	CheckValidSchema<FLSUnitStatRow>(*this);
	CheckValidSchema<FLSCastleStatRow>(*this);
	CheckValidSchema<FLSOutpostStatRow>(*this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatStatsReadFailuresTest, "Landship.Combat.Stats.ReadFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatStatsReadFailuresTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(FailureMessage, EAutomationExpectedErrorFlags::Contains, 5);
	FEnsureScope ExpectedEnsures([this](const FEnsureHandlerArgs& Args)
	{
		return HandleExpectedStatsEnsure(*this, Args);
	});
	const FLSUnitStatRow Sentinel = MakeValidRow<FLSUnitStatRow>();
	FLSUnitStatRow Output = Sentinel;
	UDataTable* Table = MakeTable(Sentinel);
	const auto CheckFailure = [&](const FDataTableRowHandle& Handle, ELSCombatStatsInitResult Expected)
	{
		TestTrue(TEXT("Failure reason is returned"), LSCombatStats::TryReadRow(Handle, Table, Output) == Expected);
		TestTrue(TEXT("Failure preserves every output field"), FLSUnitStatRow::StaticStruct()->CompareScriptStruct(&Output, &Sentinel, 0));
	};
	CheckFailure(MakeHandle(nullptr), ELSCombatStatsInitResult::MissingTable);
	CheckFailure(MakeHandle(nullptr), ELSCombatStatsInitResult::MissingTable);
	CheckFailure(MakeHandle(Table, NAME_None), ELSCombatStatsInitResult::MissingRowName);
	CheckFailure(MakeHandle(Table, TEXT("Missing")), ELSCombatStatsInitResult::RowNotFound);
	CheckFailure(MakeHandle(MakeTable(MakeValidRow<FLSCastleStatRow>())), ELSCombatStatsInitResult::WrongRowStruct);
	TestEqual(TEXT("Every failed read emits an ensure, including repeated failures"), ExpectedEnsures.GetCount(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatStatsInvalidValuesTest, "Landship.Combat.Stats.InvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatStatsInvalidValuesTest::RunTest(const FString& Parameters)
{
	// 같은 패턴을 다시 등록하면 횟수가 누적되지 않으므로 전체 예상 횟수를 한 번 등록한다.
	AddExpectedErrorPlain(FailureMessage, EAutomationExpectedErrorFlags::Contains, 47);
	FEnsureScope ExpectedEnsures([this](const FEnsureHandlerArgs& Args)
	{
		return HandleExpectedStatsEnsure(*this, Args);
	});
	CheckInvalidValues<FLSUnitStatRow>(*this);
	CheckInvalidValues<FLSCastleStatRow>(*this);
	CheckInvalidValues<FLSOutpostStatRow>(*this);
	TestEqual(TEXT("Every invalid row emits an ensure"), ExpectedEnsures.GetCount(), 47);
	return true;
}

#endif
