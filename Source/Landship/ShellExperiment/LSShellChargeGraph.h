#pragma once

#include "CoreMinimal.h"
#include "LSShellChargeStore.h"

struct LANDSHIP_API FLSShellChargeGraphNode
{
	uint64 BodyId = 0;
	bool bRelay = false;
	bool bDamageEndpoint = false;
	FLSShellChargeStore* Store = nullptr;
	TArray<uint64> Connections;
};

struct LANDSHIP_API FLSShellChargeShare
{
	FLSShellChargePacket Packet;
	uint64 RecipientBodyId = 0;
	float RequestedDamage = 0;
	/** 원본 운반 몸체부터 수신자까지 실제 접촉 연결을 따라간 경로다. 중간 몸체는 중계 몸체다. */
	TArray<uint64> PathBodyIds;
};

class LANDSHIP_API FLSShellChargeGraph
{
public:
	/**
	 * 실제로 새로 시작한 접촉 A-B의 전하를 피해 단위 Budget까지 소비한다.
	 * 호출자는 절연을 제외한 대칭 접촉 그래프와 몸체별 저장소를 제공하고 접촉 중복을 막는다.
	 * 같은 BodyId가 둘 이상이거나 접촉 끝점이 없거나 같으면 소비하지 않는다. 없는 연결 대상은 무시한다.
	 * 중계 몸체로 이어진 저장소와 인접한 말단 저장소에서 Sequence, PacketId, BodyId 순으로 소비한다.
	 * 각 원본 몸체에서 중계 몸체를 거쳐 도달하는 고유 피해 말단에 균등 분배하며 원본 몸체는 제외한다.
	 * 반환 패킷의 Amount와 RequestedDamage는 각 수신자의 몫이다. 출처 식별값은 바꾸지 않는다.
	 * 피해 말단이 없는 패킷은 소비하지 않는다. 방어·과잉 피해로 줄어든 몫을 환불하지 않는다.
	 * ContactSequence 이전에 활성화된 패킷만 소비한다. 기본값은 순서 제한이 없는 호출에 쓴다.
	 */
	static TArray<FLSShellChargeShare> ConsumeContact(const TArray<FLSShellChargeGraphNode>& Nodes,
		uint64 A, uint64 B, float Budget, uint64 ContactSequence = MAX_uint64);
};
