#pragma once

#include "CoreMinimal.h"

class ALSShellEffectInstance;
class ALSShellExperimentCoordinator;
struct FHitResult;
struct FLSShellShot;

class LANDSHIP_API FLSShellSplitRuntime
{
public:
	/** 개선 규칙의 분열 예산을 실제 자탄 또는 생성 철구 세 몸체로 한 번만 이전한다. */
	static void Execute(ALSShellExperimentCoordinator& Coordinator, FVector Point, const FLSShellShot& Shot,
		const FHitResult& Hit, bool bCombatImpact, ALSShellEffectInstance* Instance);
};
