#pragma once

#include "Engine/EngineTypes.h"

/** DefaultEngine.ini의 채널 번호와 함께 관리한다. 진영은 별도 전투 규칙으로 판정한다. */
namespace LSCollisionChannels
{
	inline constexpr ECollisionChannel Unit = ECC_GameTraceChannel1;
	inline constexpr ECollisionChannel Building = ECC_GameTraceChannel2;
	inline constexpr ECollisionChannel PathObstacle = ECC_GameTraceChannel3;
	inline constexpr ECollisionChannel Projectile = ECC_GameTraceChannel4;
	inline constexpr ECollisionChannel Ground = ECC_GameTraceChannel5;
}
