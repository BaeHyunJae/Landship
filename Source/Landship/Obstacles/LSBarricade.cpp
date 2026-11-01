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
	Visual->SetRelativeScale3D(FVector(1.4, 5, 1.8));
}
