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

	struct FLSInvalidStatCase
	{
		FFloatProperty* Property;
		float Value;
	};

	template <typename RowT>
	TArray<FLSInvalidStatCase> MakeInvalidCases(FAutomationTestBase& Test)
	{
		TArray<FLSInvalidStatCase> Cases;
		for (TFieldIterator<FProperty> It(RowT::StaticStruct()); It; ++It)
		{
			FFloatProperty* Property = CastField<FFloatProperty>(*It);
			if (!Test.TestNotNull(*FString::Printf(TEXT("%s is a float property"), *It->GetName()), Property))
			{
				continue;
			}
			Cases.Add({Property, -1.0f});
			Cases.Add({Property, std::numeric_limits<float>::infinity()});
			Cases.Add({Property, std::numeric_limits<float>::quiet_NaN()});
			if (Property->GetFName() == TEXT("Health") || Property->GetFName() == TEXT("AttackInterval"))
			{
				Cases.Add({Property, 0.0f});
			}
		}
		return Cases;
	}

	template <typename RowT>
	void CheckInvalidValues(FAutomationTestBase& Test, const TArray<FLSInvalidStatCase>& Cases)
	{
		const RowT Source = MakeValidRow<RowT>();
		UDataTable* Table = MakeTable(Source);
		const FDataTableRowHandle Handle = MakeHandle(Table);
		RowT Output = Source;
		Output.Health = 987.0f;
		const RowT Sentinel = Output;
		for (const FLSInvalidStatCase& Case : Cases)
		{
			RowT InvalidRow = Source;
			Case.Property->SetPropertyValue_InContainer(&InvalidRow, Case.Value);
			Table->AddRow(Handle.RowName, InvalidRow);
			Test.TestTrue(*FString::Printf(TEXT("%s rejects invalid value"), *Case.Property->GetName()),
				LSCombatStats::TryReadRow(Handle, Table, Output) == ELSCombatStatsInitResult::InvalidValue);
			Test.TestTrue(TEXT("Invalid row preserves every output field"), RowT::StaticStruct()->CompareScriptStruct(&Output, &Sentinel, 0));
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
	const TArray<FLSInvalidStatCase> UnitCases = MakeInvalidCases<FLSUnitStatRow>(*this);
	const TArray<FLSInvalidStatCase> CastleCases = MakeInvalidCases<FLSCastleStatRow>(*this);
	const TArray<FLSInvalidStatCase> OutpostCases = MakeInvalidCases<FLSOutpostStatRow>(*this);
	const int32 ExpectedCount = UnitCases.Num() + CastleCases.Num() + OutpostCases.Num();
	AddExpectedErrorPlain(FailureMessage, EAutomationExpectedErrorFlags::Contains, ExpectedCount);
	FEnsureScope ExpectedEnsures([this](const FEnsureHandlerArgs& Args)
	{
		return HandleExpectedStatsEnsure(*this, Args);
	});
	CheckInvalidValues<FLSUnitStatRow>(*this, UnitCases);
	CheckInvalidValues<FLSCastleStatRow>(*this, CastleCases);
	CheckInvalidValues<FLSOutpostStatRow>(*this, OutpostCases);
	TestEqual(TEXT("Every invalid row emits an ensure"), ExpectedEnsures.GetCount(), ExpectedCount);
	return true;
}

#endif
