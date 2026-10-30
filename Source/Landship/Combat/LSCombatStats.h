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
	ELSCombatStatsInitResult TryReadRow(const FDataTableRowHandle& Handle, const UObject* Owner, RowT& OutStats)
	{
		if (!Handle.DataTable)
		{
			return ReportFailure(Handle, Owner, ELSCombatStatsInitResult::MissingTable, TEXT("MissingTable"));
		}
		if (Handle.RowName.IsNone())
		{
			return ReportFailure(Handle, Owner, ELSCombatStatsInitResult::MissingRowName, TEXT("MissingRowName"));
		}
		if (Handle.DataTable->GetRowStruct() != RowT::StaticStruct())
		{
			return ReportFailure(Handle, Owner, ELSCombatStatsInitResult::WrongRowStruct, TEXT("WrongRowStruct"));
		}
		const RowT* Row = Handle.DataTable->FindRow<RowT>(Handle.RowName, TEXT("LSCombatStats::TryReadRow"), false);
		if (!Row)
		{
			return ReportFailure(Handle, Owner, ELSCombatStatsInitResult::RowNotFound, TEXT("RowNotFound"));
		}

		const RowT Candidate = *Row;
		if (const TCHAR* Reason = Validate(Candidate))
		{
			return ReportFailure(Handle, Owner, ELSCombatStatsInitResult::InvalidValue, Reason);
		}
		OutStats = Candidate;
		return ELSCombatStatsInitResult::Success;
	}
}
