#include "Obstacles/LSPathObstacle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSSelectableComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Misc/DataValidation.h"

ALSPathObstacle::ALSPathObstacle()
{
	PrimaryActorTick.bCanEverTick = false;
	Health = CreateDefaultSubobject<ULSHealthComponent>(TEXT("Health"));
	Health->bDestroyOnDeath = true;
	Health->DestroyDelay = 0;
	Vision = CreateDefaultSubobject<ULSVisionParticipantComponent>(TEXT("Vision"));
	Selection = CreateDefaultSubobject<ULSSelectableComponent>(TEXT("Selection"));
	Selection->Kind = ELSSelectionKind::Building;
	Selection->SetPlayerControlled(false);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
}
void ALSPathObstacle::SetObstacleBody(UPrimitiveComponent* Body, bool bBlocksMovement)
{
	ObstacleBody = Body;
	SetRootComponent(Body);
	Body->SetCollisionEnabled(bBlocksMovement ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(LSCollisionChannels::Building);
	Body->SetCollisionResponseToAllChannels(bBlocksMovement ? ECR_Block : ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
	Body->SetCanEverAffectNavigation(bBlocksMovement);
	Visual->SetupAttachment(Body);
}
FString ALSPathObstacle::GetConfigurationError() const
{
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0)
		return TEXT("MaxHealth must be finite and positive");
	if (!ObstacleBody || RootComponent != ObstacleBody || !ObstacleBody->IsQueryCollisionEnabled())
		return TEXT("A query-enabled authoritative root body is required");
	if (ObstacleBody->GetCollisionObjectType() != LSCollisionChannels::Building)
		return TEXT("Obstacle body must use Building object type");
	if (const UBoxComponent* Box = Cast<UBoxComponent>(ObstacleBody))
	{
		const FVector Extent = Box->GetScaledBoxExtent();
		if (Extent.ContainsNaN() || Extent.GetMin() <= 0)
			return TEXT("Obstacle box extents must be positive");
	}
	else if (const USphereComponent* Sphere = Cast<USphereComponent>(ObstacleBody))
	{
		if (!FMath::IsFinite(Sphere->GetScaledSphereRadius()) || Sphere->GetScaledSphereRadius() <= 0)
			return TEXT("Obstacle sphere radius must be positive");
	}
	else
		return TEXT("Obstacle root must be a supported sphere or box");
	return FString();
}
void ALSPathObstacle::BeginPlay()
{
	if (!Health->InitializeHealth(ELSCombatStatsInitResult::Success, MaxHealth,
								  FLSPrepareCombatDeath::CreateUObject(this, &ALSPathObstacle::PrepareForDeath),
								  GetConfigurationError()))
		DisableObstacle();
	Super::BeginPlay();
}
bool ALSPathObstacle::IsObstacleActive() const
{
	return ULSCombatTargetLibrary::IsCombatAlive(this);
}
FVector ALSPathObstacle::GetInteractionLocation() const
{
	return ObstacleBody ? ObstacleBody->GetComponentLocation() : GetActorLocation();
}
void ALSPathObstacle::DisableObstacle()
{
	Vision->NotifyCombatDeath();
	if (!IsValid(this) || IsActorBeingDestroyed())
		return;
	TInlineComponentArray<UPrimitiveComponent*> Components(this);
	for (UPrimitiveComponent* Component : Components)
	{
		if (!IsValid(this) || IsActorBeingDestroyed())
			return;
		if (!IsValid(Component))
			continue;
		Component->SetCanEverAffectNavigation(false);
		if (IsValid(Component))
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}
void ALSPathObstacle::PrepareForDeath()
{
	DisableObstacle();
}
#if WITH_EDITOR
EDataValidationResult ALSPathObstacle::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty())
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
