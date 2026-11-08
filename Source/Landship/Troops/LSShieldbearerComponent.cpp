#include "Troops/LSShieldbearerComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

void ULSShieldbearerComponent::BeginPlay()
{
	Super::BeginPlay();
	ALSTroopUnit* Unit = GetTroop();
	if (!Unit)
		return;
	Shield = NewObject<UBoxComponent>(Unit, TEXT("ArrowShield"));
	Shield->SetupAttachment(Unit->GetRootComponent());
	Shield->SetRelativeLocation(FVector(52, 0, 12));
	Shield->SetBoxExtent(FVector(6, 43, 58));
	Shield->SetCanEverAffectNavigation(false);
	Shield->SetCollisionObjectType(LSCollisionChannels::Unit);
	Shield->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shield->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Overlap);
	Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shield->RegisterComponent();

	auto* Mesh = NewObject<UStaticMeshComponent>(Unit, TEXT("ArrowShieldVisual"));
	Mesh->SetupAttachment(Shield);
	Mesh->SetRelativeScale3D(FVector(0.12, 0.86, 1.16));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->RegisterComponent();
}

void ULSShieldbearerComponent::SetBattleEnabled(bool bEnabled)
{
	Super::SetBattleEnabled(bEnabled);
	if (Shield)
		Shield->SetCollisionEnabled(IsBattleEnabled() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

bool ULSShieldbearerComponent::IsShieldSurface(const UPrimitiveComponent* Component) const
{
	return Shield && Component == Shield.Get();
}

bool ULSShieldbearerComponent::BlocksArrow(const FVector& FlightDirection) const
{
	const ALSTroopUnit* Unit = GetTroop();
	return IsBattleEnabled() && ULSCombatTargetLibrary::IsCombatAlive(Unit) &&
	       FVector::DotProduct(Unit->GetActorForwardVector(), -FlightDirection.GetSafeNormal2D()) >= 0.5;
}
