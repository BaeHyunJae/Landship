#pragma once

#include "CoreMinimal.h"
#include "Combat/LSHealthTypes.h"
#include "LSShellChargeStore.h"
#include "LSShellExperimentTypes.generated.h"

class UPrimitiveComponent;
class ULSShellReactiveComponent;

UENUM(BlueprintType)
enum class ELSShellModule : uint8 { Gravity, Blast, Split, Electric, Thrust, Iron, Adhesion, Cooling };

UENUM(BlueprintType)
enum class ELSShellRuleProfile : uint8 { Legacy, SupplyStageOne };

UENUM(BlueprintType)
enum class ELSShellSupplyStage : uint8 { Physics, Charge, Transport, Materials, Complete };

inline const TCHAR* LSShellModuleLabel(ELSShellModule Module)
{
	switch (Module)
	{
	case ELSShellModule::Gravity: return TEXT("중력");
	case ELSShellModule::Blast: return TEXT("폭발");
	case ELSShellModule::Split: return TEXT("분열");
	case ELSShellModule::Electric: return TEXT("전격");
	case ELSShellModule::Thrust: return TEXT("추진");
	case ELSShellModule::Iron: return TEXT("철구");
	case ELSShellModule::Adhesion: return TEXT("접착");
	case ELSShellModule::Cooling: return TEXT("냉각");
	}
	return TEXT("");
}

enum class ELSShellDamageCause : uint8 { Shell, Module, Impact, Detonation, Electric };
enum class ELSShellDetonationTrigger : uint8 { Damage, Impact, Proximity, WeakPoint, Shell, ModuleBlast, Chain, Discharge };
enum class ELSShellCueStyle : uint8 { Legacy, Thrust, Contact, Blast, Chain, Shatter, Charge, Discharge, Cooling, Failure };
inline constexpr float LSShellProjectileRadius = 6;

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ReloadSeconds = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ModuleDelay = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ShellSpeed = 2500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DirectDamage = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DirectVelocity = 2000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DirectLift = 300;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MuzzleExtension = 180;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastDamage = 150;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastRadius = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastVelocity = 1200;
	/** 범위 안에서 살아남은 이동 객체에 주는 최소 충격이다. 이동 저항으로 나눈다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastMinimumVelocity = 700;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastMinimumLift = 300;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityRadius = 600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravitySeconds = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityAcceleration = 2600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ImpactMinimumSpeed = 300;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ImpactDamagePerSpeed = 0.16f;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellArenaTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityFollowupDelay = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityReferenceSpeed = 1400;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityMinimumSpeed = 450;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityMaximumSpeed = 1700;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityMassExponent = .2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityPositionGain = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityResponseSeconds = .12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityMaximumAcceleration = 6000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GatheredSurfaceDistance = 180;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GatherFollowupHeight = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CoolingRadius = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CoolingChildRadius = 300;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CoolingSeconds = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float LandingNormalZ = .7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PropelledSettleSpeed = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PropelledSettleSeconds = .5f;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellSupplyTuning
{
	GENERATED_BODY()
	/** kg·cm/s 단위 운동량이다. 기존 속도 변화량 수치와 혼용하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DirectMomentum = 15000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ThrustMomentum = 240000;
	/** 후행 추진의 대상이 없어졌을 때 원래 착탄점에서 적용하는 범위(cm)다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", Units = "cm")) float ThrustRadius = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastMomentum = 360000;
	/** 모듈 한 번이 몸체에 더하는 속도의 상한(cm/s)이다. 초과 충격은 버리고 다른 대상에 나누지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", Units = "cm/s")) float ModuleMaximumDeltaSpeed = 1500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ReferenceMassKg = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ImpactDamagePerSpeed = .02f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaximumImpactDamage = 80;
	/** 환경 기폭의 방사형 충격에 더할 상향 비율이다. 모듈 폭발은 수직 상향이다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlastLiftRatio = .22f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float SurfaceOffset = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ElectricCharge = 180;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ChargeCapacity = 360;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DischargePerContact = 90;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Units = "cm/s")) float DischargeMinimumSpeed = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GravityIntegratedMomentum = 520000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GeneratedBallMassKg = 750;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GeneratedLaunchMomentum = 600000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GeneratedSurfaceGap = 120;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float GeneratedMassCapKg = 3000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 GeneratedBodyCap = 12;
	/** 새 전장만 명시적으로 사용한다. 이전 직렬화 에셋은 비활성 기본값을 유지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLSShellArenaTuning Arena;
};

USTRUCT(BlueprintType)
struct LANDSHIP_API FLSShellFeedbackTuning
{
	GENERATED_BODY()
	FLSShellFeedbackTuning(float InStrength = 1, float InDurationSeconds = .8f,
		float InFlashSeconds = .18f, float InPitchMultiplier = 1, float InCaptionHeight = 160, float InLightRadius = 0)
		: Strength(InStrength), DurationSeconds(InDurationSeconds), FlashSeconds(InFlashSeconds), PitchMultiplier(InPitchMultiplier),
		CaptionHeight(InCaptionHeight), LightRadius(InLightRadius) {}
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Strength;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DurationSeconds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FlashSeconds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PitchMultiplier;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CaptionHeight;
	/** 0이면 효과 반경을 사용한다. 방향 화살표의 길이와 조명 반경을 분리할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float LightRadius;
};

/** 발사 뒤 조합 UI와 공격자 수명에 의존하지 않는 값이다. */
struct FLSShellShot
{
	uint64 ShotId = 0;
	uint64 SourceId = 0;
	bool bPlayerResource = true;
	ELSShellRuleProfile RuleProfile = ELSShellRuleProfile::Legacy;
	FLSShellTuning BaseTuning;
	FLSShellSupplyTuning SupplyTuning;
	TWeakObjectPtr<AActor> Source;
	ELSFaction Faction = ELSFaction::Ally;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	ELSShellModule First = ELSShellModule::Gravity;
	ELSShellModule Second = ELSShellModule::Blast;
	bool bChild = false;
	bool bInheritedModule = false;
	float Damage = 50;
	float EffectScale = 1;
	TArray<FLSShellChargePacket> ChargePayload;
	int32 AdhesionSlots = -1;
	float ColdSeconds = -1;
	float GatherRadiusOverride = -1;
	float GatherSecondsOverride = -1;
};

inline bool LSShellUsesArena(const FLSShellShot& Shot)
{
	return Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.SupplyTuning.Arena.bEnabled;
}

inline float LSShellFollowupDelay(const FLSShellShot& Shot)
{
	return LSShellUsesArena(Shot) && Shot.First == ELSShellModule::Gravity
		? Shot.SupplyTuning.Arena.GravityFollowupDelay : Shot.BaseTuning.ModuleDelay;
}

struct FLSShellGatherMember
{
	TWeakObjectPtr<ULSShellReactiveComponent> Reactive;
	uint64 BodyId = 0;
	uint64 AssemblyId = 0;
	uint32 Generation = 0;
	float MassKg = 0;
	FVector Position = FVector::ZeroVector;
};

/** 발사 정보와 별개로, 후행 발동 직전에 확정한 집결 출력이다. */
struct FLSShellGatherOutput
{
	FVector Center = FVector::ZeroVector;
	TArray<FLSShellGatherMember> Members;
};

struct FLSShellGatherBudget
{
	float Radius = 0;
	float Seconds = 0;
};

struct FLSShellExplosion
{
	TWeakObjectPtr<class UPrimitiveComponent> EmitterBody;
	FVector Location = FVector::ZeroVector;
	float Damage = 250;
	float Radius = 900;
	float Velocity = 1400;
	ELSShellDamageCause Cause = ELSShellDamageCause::Detonation;
	FLSShellShot Shot;
	ELSShellDetonationTrigger Trigger = ELSShellDetonationTrigger::Damage;
	FVector TriggerOrigin = FVector::ZeroVector;
	bool bHasTriggerOrigin = false;
	TArray<uint64> IgnoredCoverBodyIds;
};

/** 판정이 끝난 반응을 표시하는 값이다. 지연 재생은 피해나 충돌을 발생시키지 않는다. */
struct FLSShellCueSpec
{
	FVector Location = FVector::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	bool bDrawLink = false;
	float Radius = 900;
	float DelaySeconds = 0;
	float Strength = 1;
	FLinearColor Color = FLinearColor(1, 0.25f, 0.025f);
	FString Caption;
	class USoundBase* Sound = nullptr;
	ELSShellCueStyle Style = ELSShellCueStyle::Legacy;
	FVector Direction = FVector::ForwardVector;
	float DurationSeconds = 0.8f;
	float FlashSeconds = 0.18f;
	float PitchMultiplier = 1;
	float CaptionHeight = 160;
	float LightRadius = 0;
	TWeakObjectPtr<UPrimitiveComponent> TargetBody;
};
