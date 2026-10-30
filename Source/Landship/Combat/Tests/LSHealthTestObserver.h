#pragma once

#include "CoreMinimal.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthTypes.h"
#include "GameFramework/Actor.h"
#include "LSHealthTestObserver.generated.h"

class UPrimitiveComponent;

/** 동적 알림을 실제 반영 함수로 받아 순서와 요청별 결과를 관찰한다. */
UCLASS()
class ULSHealthTestObserver : public UObject
{
	GENERATED_BODY()
public:
	TArray<FLSDamageResult> DamageResults;
	TArray<FLSDamageResult> DeathResults;
	TFunction<void(const FLSDamageResult&)> DamageCallback;
	TFunction<void(const FLSDamageResult&)> DeathCallback;
	TFunction<void(UPrimitiveComponent*)> CollisionCallback;

	UFUNCTION()
	void OnDamage(const FLSDamageResult& Damage);

	UFUNCTION()
	void OnDeath(const FLSDamageResult& Damage);

	UFUNCTION()
	void OnCollisionChanged(UPrimitiveComponent* ChangedComponent);
};

/** 콘텐츠 수치나 BeginPlay에 의존하지 않고 진영과 피해의 경계를 검사한다. */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class ALSHealthTestActor : public AActor, public ILSFactionInterface
{
	GENERATED_BODY()
public:
	ELSFaction Faction = ELSFaction::Ally;
	int32 NativeDeathCount = 0;
	void PrepareForTestDeath() { ++NativeDeathCount; }
	virtual ELSFaction GetFaction() const override { return Faction; }
};
