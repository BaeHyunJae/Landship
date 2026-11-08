#include "Troops/LSTroopUnit.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Controls/LSSelectableComponent.h"
#include "Controls/LSRTSHUD.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

ALSTroopUnit::ALSTroopUnit()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	RangedAttack.ProjectileClass = ALSBasicProjectile::StaticClass();
	RangedAttack.VisualStyle = ELSProjectileVisualStyle::Arrow;
	MeleeIdleAnimation = TSoftObjectPtr<UAnimSequence>(
	    FSoftObjectPath(TEXT("/Game/ReflectionLab/Gameplay/Player/Animations/A_2HandIdle.A_2HandIdle")));
	MeleeAttackAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(
	    TEXT("/Game/ReflectionLab/Gameplay/Player/Animations/A_Parry_BaseballHit.A_Parry_BaseballHit")));
	ArcherIdleAnimation = TSoftObjectPtr<UAnimSequence>(
	    FSoftObjectPath(TEXT("/Game/ReflectionLab/Gameplay/Enemies/Animations/A_Enemy_Idle.A_Enemy_Idle")));
	ArcherAttackAnimation = TSoftObjectPtr<UAnimSequence>(
	    FSoftObjectPath(TEXT("/Game/ReflectionLab/Gameplay/Enemies/Animations/A_Enemy_Shoot.A_Enemy_Shoot")));
	TroopStatsTable =
	    TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Landship/Troops/Data/DT_TroopStats.DT_TroopStats")));
	PrototypeMaterial = TSoftObjectPtr<UMaterialInterface>(
	    FSoftObjectPath(TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SpearShape = Cube.Object;
	ArcherShape = Cone.Object;
	CavalryShape = Sphere.Object;
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Avatar(
	    TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	GetMesh()->SetSkeletalMeshAsset(Avatar.Object);
	GetMesh()->SetRelativeLocation(FVector(0, 0, -88));
	GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCanEverAffectNavigation(false);
	bUseRagdoll = true;
	// 기존 맵의 서브오브젝트 이름은 유지하고 역할만 머리 위 표식으로 변경한다.
	TroopMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeBody"));
	TroopMarker->SetupAttachment(RootComponent);
	// 이동·피격·클릭의 충돌은 부모 캡슐이 소유한다.
	TroopMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TroopMarker->SetCanEverAffectNavigation(false);
	TroopMarker->SetCastShadow(false);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	TroopLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TroopLabel"));
	TroopLabel->SetupAttachment(RootComponent);
	TroopLabel->SetRelativeLocation(FVector(0, 0, 185));
	TroopLabel->SetRelativeRotation(FRotator(0, 180, 0));
	TroopLabel->SetHorizontalAlignment(EHTA_Center);
	TroopLabel->SetWorldSize(27.0f);
}

FName ALSTroopUnit::GetStatsRowName(ELSTroopType Type)
{
	switch (Type)
	{
	case ELSTroopType::Spearman:
		return TEXT("Spearman");
	case ELSTroopType::Archer:
		return TEXT("Archer");
	case ELSTroopType::Cavalry:
		return TEXT("Cavalry");
	default:
		return NAME_None;
	}
}

void ALSTroopUnit::GetCapsuleDimensions(ELSTroopType Type, float& OutRadius, float& OutHalfHeight)
{
	OutRadius = Type == ELSTroopType::Cavalry ? 65.0f : 42.0f;
	OutHalfHeight = 88.0f;
}

bool ALSTroopUnit::SetInitialTroopType(ELSTroopType InType)
{
	if (bTroopTypeLocked || GetStatsRowName(InType).IsNone())
	{
		return false;
	}
	TroopType = InType;
	RefreshDefinition();
	return true;
}

void ALSTroopUnit::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshDefinition();
}

void ALSTroopUnit::RefreshDefinition()
{
	StatsRow.DataTable = TroopStatsTable.LoadSynchronous();
	StatsRow.RowName = GetStatsRowName(TroopType);
	Selection->SelectionType = StatsRow.RowName;
	const TCHAR* GroupNames[] = {TEXT("창병"), TEXT("궁병"), TEXT("기마병")};
	if (static_cast<uint8>(TroopType) < UE_ARRAY_COUNT(GroupNames))
		Selection->CommandGroupName = FText::FromString(GroupNames[static_cast<uint8>(TroopType)]);
	UStaticMesh* Shape = nullptr;
	FVector Scale = FVector::OneVector;
	switch (TroopType)
	{
	case ELSTroopType::Spearman:
		Shape = SpearShape;
		Scale = FVector(0.35f);
		break;
	case ELSTroopType::Archer:
		Shape = ArcherShape;
		Scale = FVector(0.4f);
		break;
	case ELSTroopType::Cavalry:
		Shape = CavalryShape;
		Scale = FVector(0.4f);
		break;
	}
	TroopMarker->SetStaticMesh(Shape);
	TroopMarker->SetRelativeScale3D(Scale);
	TroopMarker->SetRelativeLocation(FVector(0, 0, 145));
	float Radius, HalfHeight;
	GetCapsuleDimensions(TroopType, Radius, HalfHeight);
	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
	const bool bAlly = GetFaction() == ELSFaction::Ally;
	MarkerColor = bAlly ? FLinearColor(0.08f, 0.55f, 1.0f) : FLinearColor(0.95f, 0.12f, 0.05f);
	if (UMaterialInterface* Material = PrototypeMaterial.LoadSynchronous())
	{
		MarkerMaterial = UMaterialInstanceDynamic::Create(Material, this);
		MarkerMaterial->SetVectorParameterValue(TEXT("TeamColor"), MarkerColor);
		TroopMarker->SetMaterial(0, MarkerMaterial);
	}
	FLSProjectileAttackSettings Shot = TroopType == ELSTroopType::Archer ? RangedAttack : FLSProjectileAttackSettings();
	if (Shot.ProjectileClass && !Shot.Material)
	{
		Shot.Material = PrototypeMaterial.LoadSynchronous();
	}
	BasicAttack->ConfigureProjectile(Shot);
	TroopLabel->SetText(FText::FromName(StatsRow.RowName));
	TroopLabel->SetTextRenderColor(bAlly ? FColor::Cyan : FColor::Red);
}

void ALSTroopUnit::BeginPlay()
{
	bTroopTypeLocked = true;
	// 부모의 체력·공격·이동 초기화 전에 최종 병종의 데이터 행을 확정한다.
	RefreshDefinition();
	Super::BeginPlay();
	Health->OnDiedEvent.AddDynamic(this, &ALSTroopUnit::OnTroopDied);
	Health->OnDamagedEvent.AddDynamic(this, &ALSTroopUnit::OnTroopDamaged);
	if (!IsGameplayStaged()) InitializeAnimations();
}

void ALSTroopUnit::ActivateProducedUnit()
{
	Super::ActivateProducedUnit();
	InitializeAnimations();
}

void ALSTroopUnit::InitializeAnimations()
{
	if (!ULSCombatTargetLibrary::IsCombatAlive(this))
		return;
	const bool bArcher = TroopType == ELSTroopType::Archer;
	UAnimSequence* Idle = (bArcher ? ArcherIdleAnimation : MeleeIdleAnimation).LoadSynchronous();
	UAnimSequence* Attack = (bArcher ? ArcherAttackAnimation : MeleeAttackAnimation).LoadSynchronous();
	const USkeletalMesh* MeshAsset = GetMesh()->GetSkeletalMeshAsset();
	const auto IsValidClip = [MeshAsset](const UAnimSequence* Clip)
	{
		return Clip && MeshAsset && Clip->GetSkeleton() == MeshAsset->GetSkeleton() &&
		       FMath::IsFinite(Clip->GetPlayLength()) && Clip->GetPlayLength() > 0 &&
		       FMath::IsFinite(Clip->RateScale) && Clip->RateScale > 0;
	};
	if (!IsValidClip(Idle) || !IsValidClip(Attack))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 병종 애니메이션이 없거나 메시·재생 설정과 호환되지 않습니다"), *GetName());
		return;
	}
	ActiveIdleAnimation = Idle;
	ActiveAttackAnimation = Attack;
	BasicAttack->OnAttackExecutedEvent.AddUniqueDynamic(this, &ALSTroopUnit::OnTroopAttackExecuted);
	PlayIdleAnimation();
}

void ALSTroopUnit::PlayIdleAnimation()
{
	GetWorldTimerManager().ClearTimer(AttackAnimationTimer);
	VisualAttackTarget.Reset();
	SetActorTickEnabled(false);
	if (ActiveIdleAnimation && ULSCombatTargetLibrary::IsCombatAlive(this) && !GetMesh()->IsSimulatingPhysics())
	{
		GetMesh()->PlayAnimation(ActiveIdleAnimation, true);
		GetMesh()->SetPlayRate(1.0f);
	}
}

void ALSTroopUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 명령 실행부를 구독하지 않아도 직접 StopAttack·표적 교체를 같은 표현 규칙으로 처리한다.
	if (!VisualAttackTarget.IsValid() || BasicAttack->GetAttackTarget() != VisualAttackTarget.Get() ||
	    !ULSCombatTargetLibrary::IsCombatAlive(VisualAttackTarget.Get()))
		PlayIdleAnimation();
}

void ALSTroopUnit::OnTroopDamaged(const FLSDamageResult& Damage)
{
	if (ALSRTSHUD::UsesUnifiedCombatFeedback(GetWorld()) || !MarkerMaterial || !ULSCombatTargetLibrary::IsCombatAlive(this) || !FMath::IsFinite(HitFlashDuration) ||
	    HitFlashDuration <= 0)
		return;
	MarkerMaterial->SetVectorParameterValue(TEXT("TeamColor"), HitFlashColor);
	GetWorldTimerManager().SetTimer(HitFlashTimer, this, &ALSTroopUnit::RestoreMarkerColor, HitFlashDuration, false);
}

void ALSTroopUnit::RestoreMarkerColor()
{
	if (MarkerMaterial)
		MarkerMaterial->SetVectorParameterValue(TEXT("TeamColor"), MarkerColor);
}

void ALSTroopUnit::OnTroopAttackExecuted(const FLSAttackExecution& Execution)
{
	if (Execution.Source.Get() != this || !ActiveAttackAnimation || !ULSCombatTargetLibrary::IsCombatAlive(this) ||
	    GetMesh()->IsSimulatingPhysics())
		return;
	// 피해와 투사체 발사는 기존 전투 코드가 소유한다. 애니메이션은 확정된 공격만 표현한다.
	const float Interval = BasicAttack->GetEffectiveAttackInterval();
	const float Length = ActiveAttackAnimation->GetPlayLength() / ActiveAttackAnimation->RateScale;
	if (!FMath::IsFinite(Interval) || Interval <= 0 || !FMath::IsFinite(Length) || Length <= 0)
		return;
	const float PlayRate = FMath::Max(1.0f, Length / Interval);
	if (!FMath::IsFinite(PlayRate))
		return;
	FVector Direction = Execution.TargetLocation - GetActorLocation();
	Direction.Z = 0;
	if (!Direction.IsNearlyZero())
		SetActorRotation(Direction.Rotation());
	VisualAttackTarget = Execution.Target;
	SetActorTickEnabled(true);
	GetWorldTimerManager().ClearTimer(AttackAnimationTimer);
	GetMesh()->PlayAnimation(ActiveAttackAnimation, false);
	GetMesh()->SetPlayRate(PlayRate);
	GetWorldTimerManager().SetTimer(AttackAnimationTimer, this, &ALSTroopUnit::PlayIdleAnimation, Length / PlayRate,
	                                false);
}

void ALSTroopUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AttackAnimationTimer);
	GetWorldTimerManager().ClearTimer(HitFlashTimer);
	Health->OnDamagedEvent.RemoveDynamic(this, &ALSTroopUnit::OnTroopDamaged);
	BasicAttack->OnAttackExecutedEvent.RemoveDynamic(this, &ALSTroopUnit::OnTroopAttackExecuted);
	Health->OnDiedEvent.RemoveDynamic(this, &ALSTroopUnit::OnTroopDied);
	Super::EndPlay(EndPlayReason);
}

void ALSTroopUnit::OnTroopDied(const FLSDamageResult& Damage)
{
	SetActorTickEnabled(false);
	VisualAttackTarget.Reset();
	GetWorldTimerManager().ClearTimer(HitFlashTimer);
	GetWorldTimerManager().ClearTimer(AttackAnimationTimer);
	BasicAttack->OnAttackExecutedEvent.RemoveDynamic(this, &ALSTroopUnit::OnTroopAttackExecuted);
	TroopMarker->SetVisibility(false);
	TroopLabel->SetVisibility(false);
}

#if WITH_EDITOR
bool ALSTroopUnit::CanEditChange(const FProperty* Property) const
{
	if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(ALSTroopUnit, StatsRow))
	{
		return false;
	}
	if (Property && bTroopTypeLocked && Property->GetFName() == GET_MEMBER_NAME_CHECKED(ALSTroopUnit, TroopType))
	{
		return false;
	}
	return Super::CanEditChange(Property);
}
#endif
