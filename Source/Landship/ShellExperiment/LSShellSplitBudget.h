#pragma once

#include "CoreMinimal.h"
#include "LSShellChargeStore.h"
#include "LSShellExperimentTypes.h"

enum class ELSShellSplitOrder : uint8 { SplitFirst, SplitSecond };

struct LANDSHIP_API FLSShellSplitPortion
{
	float EffectScale = 0;
	float DirectDamage = 0;
	int32 AdhesionSlots = 0;
	float ColdSeconds = 0;
};

struct LANDSHIP_API FLSShellSplitAllocation
{
	bool bValid = false;
	FLSShellSplitPortion Parent;
	FLSShellSplitPortion Children[3];
	bool bExportRemainingCharge = false;
	bool bExportRemainingGravity = false;
	bool bSplitExistingGeneratedMass = false;
};

class LANDSHIP_API FLSShellSplitBudget
{
public:
	/**
	 * 개선 규칙의 분열 한 번에 쓸 예산을 계산한다. 상태를 소비하거나 새 자원을 예약하지 않는다.
	 * DirectDamage는 체력 피해 단위이며, ColdSeconds는 게임 초다. 음수·비유한 수치와 분열 자체의 조합은 거부한다.
	 * Export 플래그가 켜진 자원은 실행 시 남은 양만 내보낸다. 기존 철구의 분할도 새 질량 예약을 대신한다.
	 */
	static FLSShellSplitAllocation Allocate(ELSShellModule PairedModule, ELSShellSplitOrder Order,
		float OriginalDirectDamage = 50, int32 AdhesionSlots = 3, float ColdSeconds = 4);

	/**
	 * 이미 인출한 잔여 전하 한 패킷을 세 자식으로 나눈다. 입력·저장소는 바꾸지 않는다.
	 * 호출자가 전역 고유 ID를 발급한다. 0·원본 ID·자식 간 중복 ID면 빈 결과를 반환한다.
	 * 실패해도 호출한 발급기의 ID는 되돌리지 않는다. 성공 시 새 ID·부모 ID·Amount 외의 메타데이터는 보존한다.
	 * 새 몸체에 저장할 때의 CarrierBodyId와 ActivationSequence 지정은 호출자가 맡는다.
	 */
	static TArray<FLSShellChargePacket> FragmentChargePacket(const FLSShellChargePacket& RemainingPacket,
		TFunctionRef<uint64()> AllocatePacketId);
};
