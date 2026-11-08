#pragma once

#include "CoreMinimal.h"
#include "LSShellExperimentTypes.h"

class ALSShellMetalBall;
class UWorld;
struct FHitResult;

/** 한 실험 조정자가 소유하며 실제 생성 몸체만 집계한다. 비행 중 예약은 보유하지 않는다. */
class LANDSHIP_API FLSShellGeneratedMaterialRegistry
{
public:
	ALSShellMetalBall* GenerateIron(UWorld* World, const FHitResult& Hit, const FLSShellShot& Shot, FString& OutError);
	bool Recall(AActor* Actor);
	bool IsGenerated(const AActor* Actor) const;
	/** 반환 포인터는 다음 레지스트리 변경까지만 유효하다. 장기 보관할 때는 값을 복사한다. */
	const FLSShellShot* GetOrigin(const AActor* Actor) const;
	float GetLiveMassKg() const;
	int32 GetLiveBodyCount() const;
	/** 원본이 물리적으로 제거되기 전에 호출한다. 얼음 등 다른 Actor 형식으로도 출처와 예산을 잇는다. */
	bool Remap(AActor* Original, AActor* Replacement);

	using FPrepareReplacement = TFunction<bool(const AActor*, const TArray<ALSShellMetalBall*>&, FString&)>;
	using FCommitReplacement = TFunction<void(AActor*, const TArray<ALSShellMetalBall*>&)>;
	/**
	 * 세 자세·질량·실제 몸체가 모두 준비된 뒤에만 Commit을 호출하고 원본을 제거한다.
	 * Prepare는 원본과 외부 상태를 바꾸지 않는다. Commit은 실패하지 않으며 전하·결속·몸체 핸들을
	 * 합산량 보존으로 이전한다. 그 전의 실패는 자식만 제거하고 원본을 그대로 둔다.
	 */
	bool TryReplaceThree(AActor* Parent, const TArray<FTransform>& Poses, const FPrepareReplacement& Prepare,
		const FCommitReplacement& Commit, TArray<ALSShellMetalBall*>& OutChildren, FString& OutError);

private:
	struct FLSEntry
	{
		FLSShellShot Origin;
		float MassKg = 0;
		float BallRadius = 70;
	};
	void Prune();
	TMap<TWeakObjectPtr<AActor>, FLSEntry> Entries;
};
