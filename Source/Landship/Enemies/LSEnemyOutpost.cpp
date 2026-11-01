#include "Enemies/LSEnemyOutpost.h"

#include "Controls/LSOrderComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ALSEnemyOutpost::ALSEnemyOutpost()
{
	Orders->PrimaryComponentTick.bStartWithTickEnabled = false;
	Behavior = CreateDefaultSubobject<ULSEnemyBehaviorComponent>(TEXT("EnemyBehavior"));
	static ConstructorHelpers::FObjectFinder<UDataTable> Table(
		TEXT("/Game/Landship/Combat/Data/DT_OutpostStats.DT_OutpostStats"));
	StatsRow.DataTable = Table.Object;
	StatsRow.RowName = TEXT("PrototypeTest");
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	// 큰 성용 탐색 반경과 기본19cm 셀에서도 250cm 근접 사거리 안에 설 수 있는 시제품 크기다.
	Collision->SetBoxExtent(FVector(80, 80, 100));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OutpostBody"));
	Body->SetupAttachment(RootComponent);
	Body->SetStaticMesh(Cube.Object);
	Body->SetRelativeScale3D(FVector(1.6f, 1.6f, 2.0f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	Body->SetMaterial(0, Material.Object);
}

void ALSEnemyOutpost::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* Material = Body->CreateDynamicMaterialInstance(0))
	{
		Material->SetVectorParameterValue(TEXT("TeamColor"), GetFaction() == ELSFaction::Ally
			? FLinearColor(0.08f, 0.55f, 1.0f) : FLinearColor(0.95f, 0.12f, 0.05f));
	}
}
