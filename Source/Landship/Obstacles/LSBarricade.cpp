#include "Obstacles/LSBarricade.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
ALSBarricade::ALSBarricade()
{
	MaxHealth = 1000;
	UBoxComponent* Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(70, 250, 90));
	SetObstacleBody(Body, true);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Mesh.Object);
	Visual->SetRelativeLocation(FVector(0, 0, 35));
	Visual->SetRelativeScale3D(FVector(.25, 5, .22));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		UStaticMeshComponent* Post = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("FencePost%d"), Index));
		Post->SetupAttachment(Body);
		Post->SetStaticMesh(Mesh.Object);
		Post->SetRelativeLocation(FVector(0, -220 + Index * 110, 0));
		Post->SetRelativeScale3D(FVector(.3, .3, 1.8));
		Post->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Post->SetCanEverAffectNavigation(false);
	}
	UStaticMeshComponent* LowerRail = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LowerRail"));
	LowerRail->SetupAttachment(Body);
	LowerRail->SetStaticMesh(Mesh.Object);
	LowerRail->SetRelativeLocation(FVector(0, 0, -35));
	LowerRail->SetRelativeScale3D(FVector(.25, 5, .22));
	LowerRail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LowerRail->SetCanEverAffectNavigation(false);
}
