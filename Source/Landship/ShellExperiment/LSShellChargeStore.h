#pragma once

#include "CoreMinimal.h"
#include "Combat/LSFaction.h"

class AActor;

enum class ELSShellChargeProvenance : uint8 { Player, Environment };

struct LANDSHIP_API FLSShellChargePacket
{
	uint64 PacketId = 0;
	uint64 ParentPacketId = 0;
	/** 분할 전 원본 전하의 생성 순서다. 자식 패킷도 이 순서를 유지한다. */
	uint64 Sequence = 0;
	uint64 SourceId = 0;
	uint64 ShotId = 0;
	uint64 CarrierBodyId = 0;
	/** 현재 운반 몸체의 저장소에 활성화된 순서다. 출처 식별값과 별개이며 소비 중에는 유지한다. */
	uint64 ActivationSequence = 0;
	TWeakObjectPtr<AActor> Source;
	ELSFaction Faction = ELSFaction::Ally;
	ELSShellChargeProvenance Provenance = ELSShellChargeProvenance::Environment;
	/** 체력 피해와 같은 단위의 전하량이다. 질량이나 충격량이 아니다. */
	float Amount = 0;
};

class LANDSHIP_API FLSShellChargeStore
{
public:
	/**
	 * 남은 용량만큼 새 패킷을 복사하고 실제 수용량을 반환한다. 용량과 전하량은 체력 피해 단위다.
	 * 유한한 양수가 아닌 수치나 이미 저장 중인 PacketId는 거부한다. 기존 패킷은 바꾸지 않는다.
	 * PacketId, SourceId, Sequence 또는 ActivationSequence가 0이면 정체성·활성 순서가 없는 입력으로 거부한다.
	 * CarrierBodyId만 현재 운반 몸체로 지정하며, 나머지 출처와 식별값은 원본을 보존한다.
	 */
	float Add(const FLSShellChargePacket& Packet, float Capacity, uint64 CarrierBodyId);
	float GetStoredCharge() const;
	const TArray<FLSShellChargePacket>& GetPackets() const { return Packets; }

	/**
	 * 지정 패킷에서 요청량까지 소비하고 원래 출처·식별값을 가진 소비분을 반환한다.
	 * 요청량은 체력 피해 단위의 유한한 양수여야 한다. 실패하면 Amount가 0이다.
	 * 없는 패킷은 요청한 PacketId만 담아 반환한다. 남은 패킷의 순서는 유지한다.
	 */
	FLSShellChargePacket ConsumePacket(uint64 PacketId, float RequestedAmount);
	void Clear();
	bool IsEmpty() const { return Packets.IsEmpty(); }

private:
	TArray<FLSShellChargePacket> Packets;
};
