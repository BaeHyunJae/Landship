#include "Castle/LSMobileCastle.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ALSMobileCastle::ALSMobileCastle()
{
	static ConstructorHelpers::FObjectFinder<UDataTable> Table(
	    TEXT("/Game/Landship/Troops/Data/DT_TroopStats.DT_TroopStats"));
	StatsRow.DataTable = Table.Object;
	StatsRow.RowName = TEXT("MobileCastle");
	Selection->Kind = ELSSelectionKind::Castle;
	Selection->SelectionType = TEXT("MobileCastle");
	Health->bDestroyOnDeath = false;
	GetCapsuleComponent()->SetCapsuleSize(142, 142);
	GetCapsuleComponent()->SetCollisionObjectType(LSCollisionChannels::Building);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetCharacterMovement()->NavAgentProps.AgentRadius = 142;
	GetCharacterMovement()->NavAgentProps.AgentHeight = 284;
	// 큰 성은 진행 방향을 유지하고 주변 병력이 성을 우선 회피한다.
	GetCharacterMovement()->AvoidanceWeight = 1.0f;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	LeftTower = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftTower"));
	RightTower = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightTower"));
	for (UStaticMeshComponent* Part : {Body.Get(), LeftTower.Get(), RightTower.Get()})
	{
		Part->SetupAttachment(RootComponent);
		Part->SetStaticMesh(Cube.Object);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
	}
	Body->SetRelativeLocation(FVector(0, 0, -72));
	Body->SetRelativeScale3D(FVector(2, 1.8, 1.2));
	LeftTower->SetRelativeLocation(FVector(0, -55, 38));
	RightTower->SetRelativeLocation(FVector(0, 55, 38));
	LeftTower->SetRelativeScale3D(FVector(0.8, 0.7, 1));
	RightTower->SetRelativeScale3D(FVector(0.8, 0.7, 1));
}

void ALSMobileCastle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateTeamColor();
}

void ALSMobileCastle::BeginPlay()
{
	Super::BeginPlay();
	UpdateTeamColor();
}

void ALSMobileCastle::UpdateTeamColor()
{
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(
	    nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (!Material)
	{
		return;
	}
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
	Instance->SetVectorParameterValue(TEXT("TeamColor"), GetFaction() == ELSFaction::Ally
	                                                         ? FLinearColor(0.08f, 0.55f, 1)
	                                                         : FLinearColor(0.95f, 0.12f, 0.05f));
	for (UStaticMeshComponent* Part : {Body.Get(), LeftTower.Get(), RightTower.Get()})
	{
		Part->SetMaterial(0, Instance);
	}
}
