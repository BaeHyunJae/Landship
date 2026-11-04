#pragma once

#include "NavigationPath.h"

class APawn;

namespace LSCastlePathGeometry
{
/** 선회 모드인 실제 성의 새 NavMesh 경로에만 적용한다. 지뢰 검사·조각 연결·추종 전에 한 번 호출한다. */
void OffsetCorners(const APawn* Pawn, const FNavPathSharedPtr& Path);
}
