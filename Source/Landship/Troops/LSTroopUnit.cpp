#include "Troops/LSTroopUnit.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"

#include "Controls/LSSelectableComponent.h"
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
	RangedAttack.ProjectileClass = ALSBasicProjectile::StaticClass();
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
	GetCapsuleComponent()->SetCapsuleSize(TroopType == ELSTroopType::Cavalry ? 65.0f : 42.0f, 88.0f);
	const bool bAlly = GetFaction() == ELSFaction::Ally;
	const FLinearColor TeamColor = bAlly ? FLinearColor(0.08f, 0.55f, 1.0f) : FLinearColor(0.95f, 0.12f, 0.05f);
	if (UMaterialInterface* Material = PrototypeMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
		Instance->SetVectorParameterValue(TEXT("TeamColor"), TeamColor);
		TroopMarker->SetMaterial(0, Instance);
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
}

void ALSTroopUnit::OnTroopDied(const FLSDamageResult& Damage)
{
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
