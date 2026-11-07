#include "Match/LSAutoBattleEnemyMarker.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

ALSAutoBattleEnemyMarker::ALSAutoBattleEnemyMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bIsEditorOnlyActor = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GroundPivot"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Preview"));
	Body->SetupAttachment(RootComponent);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0, 0, 210));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetTextRenderColor(FColor(255, 60, 30));
	Label->SetWorldSize(36);
}

void ALSAutoBattleEnemyMarker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	const TCHAR* Shapes[] = {TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/Engine/BasicShapes/Cone.Cone"),
	                         TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder")};
	const uint8 Index = static_cast<uint8>(TroopType);
	if (Index >= UE_ARRAY_COUNT(Shapes))
	{
		Label->SetText(FText::FromString(TEXT("INVALID TYPE")));
		return;
	}
	Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Shapes[Index]));
	const float Radius = TroopType == ELSTroopType::Cavalry ? 65 : 42;
	Body->SetRelativeScale3D(FVector(Radius / 50, Radius / 50, 1.76));
	Body->SetRelativeLocation(FVector(0, 0, 88));
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(
	        nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype")))
	{
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
		Dynamic->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(1, 0.12f, 0.03f));
		Body->SetMaterial(0, Dynamic);
	}
	Label->SetText(FText::FromString(TEXT("ENEMY / ") + ALSTroopUnit::GetStatsRowName(TroopType).ToString()));
#endif
}

void ALSAutoBattleEnemyMarker::BeginPlay()
{
	Super::BeginPlay();
	Destroy();
}
