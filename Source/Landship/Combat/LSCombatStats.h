#pragma once

#include "CoreMinimal.h"
#include "LSCombatStatRows.h"
#include "LSCombatStats.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLSCombatStats, Log, All);

/** 초기화 시도 여부와 실패 이유를 구분한다. */
UENUM(BlueprintType)
enum class ELSCombatStatsInitResult : uint8
{
	NotAttempted,
	Success,
	MissingTable,
	MissingRowName,
	RowNotFound,
	WrongRowStruct,
	InvalidValue
};

namespace LSCombatStats
{
	/** 유효하지 않은 필드의 설명을 반환한다. 모든 값이 유효하면 nullptr이다. */
	LANDSHIP_API const TCHAR* Validate(const FLSUnitStatRow& Stats);
	LANDSHIP_API const TCHAR* Validate(const FLSCastleStatRow& Stats);
	LANDSHIP_API const TCHAR* Validate(const FLSOutpostStatRow& Stats);

	/** 진단 매크로 설정에 관계없이 실패 결과를 반환한다. */
	LANDSHIP_API ELSCombatStatsInitResult ReportFailure(const FDataTableRowHandle& Handle,
		const UObject* Owner, ELSCombatStatsInitResult Result, const TCHAR* Reason);

	/** 행 포인터를 보관하지 않으며, 검증에 성공했을 때만 출력을 바꾼다. */
	template <typename RowT>
	ELSCombatStatsInitResult InspectRow(const FDataTableRowHandle& Handle, RowT& OutStats, const TCHAR*& OutFailureReason)
	{
		OutFailureReason = nullptr;
		if (!Handle.DataTable)
		{
			OutFailureReason = TEXT("MissingTable");
			return ELSCombatStatsInitResult::MissingTable;
		}
		if (Handle.RowName.IsNone())
		{
			OutFailureReason = TEXT("MissingRowName");
			return ELSCombatStatsInitResult::MissingRowName;
		}
		if (Handle.DataTable->GetRowStruct() != RowT::StaticStruct())
		{
			OutFailureReason = TEXT("WrongRowStruct");
			return ELSCombatStatsInitResult::WrongRowStruct;
		}
		const RowT* Row = Handle.DataTable->FindRow<RowT>(Handle.RowName, TEXT("LSCombatStats::TryReadRow"), false);
		if (!Row)
		{
			OutFailureReason = TEXT("RowNotFound");
			return ELSCombatStatsInitResult::RowNotFound;
		}

		const RowT Candidate = *Row;
		if (const TCHAR* Reason = Validate(Candidate))
		{
			OutFailureReason = Reason;
			return ELSCombatStatsInitResult::InvalidValue;
		}
		OutStats = Candidate;
		return ELSCombatStatsInitResult::Success;
	}

	template <typename RowT>
	ELSCombatStatsInitResult TryReadRow(const FDataTableRowHandle& Handle, const UObject* Owner, RowT& OutStats)
	{
		const TCHAR* FailureReason = nullptr;
		const ELSCombatStatsInitResult Result = InspectRow(Handle, OutStats, FailureReason);
		return Result == ELSCombatStatsInitResult::Success
			? Result : ReportFailure(Handle, Owner, Result, FailureReason);
	}
}
