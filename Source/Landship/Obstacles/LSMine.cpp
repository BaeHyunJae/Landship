#include "Obstacles/LSMine.h"
#include "Obstacles/LSMineHazardSubsystem.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Castle/LSCastle.h"
#include "Castle/LSMobileCastle.h"
#include "Troops/LSUnit.h"
#include "Controls/LSSelectableComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ALSMine::ALSMine()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	MaxHealth = 50;
	Health->bDestroyOnDeath = false;
	USphereComponent* Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	Body->SetSphereRadius(35);
	SetObstacleBody(Body, false);
	Body->SetGenerateOverlapEvents(false);
	ContactSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ContactSphere"));
	ContactSphere->SetupAttachment(Body);
	ContactSphere->SetSphereRadius(100);
	ContactSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContactSphere->SetCollisionObjectType(ECC_WorldDynamic);
	ContactSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContactSphere->SetCollisionResponseToChannel(LSCollisionChannels::Unit, ECR_Overlap);
	ContactSphere->SetCollisionResponseToChannel(LSCollisionChannels::Building, ECR_Overlap);
	ContactSphere->SetGenerateOverlapEvents(true);
	ContactSphere->SetCanEverAffectNavigation(false);
	Vision->bConcealedOutsideSight = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Visual->SetStaticMesh(Sphere.Object);
	Visual->SetRelativeScale3D(FVector(.7));
	ProgressBackground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProgressBackground"));
	ProgressFill = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProgressFill"));
	for (UStaticMeshComponent* Part : {ProgressBackground.Get(), ProgressFill.Get()})
	{
		Part->SetupAttachment(Body);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetVisibility(false);
	}
	ProgressBackground->SetStaticMesh(Cube.Object);
	ProgressFill->SetStaticMesh(Cube.Object);
	ProgressBackground->SetRelativeLocation(FVector(0, 0, 80));
	ProgressBackground->SetRelativeScale3D(FVector(1, .12, .08));
	ProgressFill->SetRelativeLocation(FVector(0, 0, 86));
}
FVector ALSMine::GetTriggerCenter() const
{
	return ContactSphere->GetComponentLocation();
}
float ALSMine::GetTriggerRadius() const
{
	return ContactSphere->GetScaledSphereRadius();
}
bool ALSMine::IsArmed() const
{
	return State == ELSMineState::Armed && Health->GetInitializationState() == ELSHealthInitializationState::Ready;
}
FString ALSMine::GetConfigurationError() const
{
	const FString Parent = Super::GetConfigurationError();
	if (!Parent.IsEmpty())
		return Parent;
	if (!GetActorScale3D().Equals(FVector::OneVector))
		return TEXT("Mine actor scale must be one; scale only visual meshes");
	if (!ContactSphere->GetRelativeScale3D().Equals(FVector::OneVector))
		return TEXT("Mine contact sphere scale must be one; edit its radius instead");
	for (float Value : {ExplosionRadius, DisarmDistance, DisarmDuration, ChainDelay, GetTriggerRadius()})
		if (!FMath::IsFinite(Value) || Value <= 0)
			return TEXT("Mine ranges and durations must be finite and positive");
	if (GetTriggerRadius() >= DisarmDistance)
		return TEXT("Mine contact radius must be smaller than disarm distance");
	if (!FMath::IsFinite(CastleMaxHealthFraction) || CastleMaxHealthFraction < 0 || CastleMaxHealthFraction > 1)
		return TEXT("Castle damage fraction must be in [0,1]");
	return FString();
}
void ALSMine::BeginPlay()
{
	Health->OnDamagedEvent.AddDynamic(this, &ALSMine::OnDamaged);
	ContactSphere->OnComponentBeginOverlap.AddDynamic(this, &ALSMine::OnContact);
	Super::BeginPlay();
	if (Health->GetInitializationState() != ELSHealthInitializationState::Ready)
		return;
	UMaterialInterface* PrototypeMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	if (PrototypeMaterial)
	{
		MineMaterial = UMaterialInstanceDynamic::Create(PrototypeMaterial, this);
		MineMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.35f, .12f, .04f));
		Visual->SetMaterial(0, MineMaterial);
		UMaterialInstanceDynamic* Background = UMaterialInstanceDynamic::Create(PrototypeMaterial, this);
		Background->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.015f, .015f, .015f));
		ProgressBackground->SetMaterial(0, Background);
		UMaterialInstanceDynamic* Fill = UMaterialInstanceDynamic::Create(PrototypeMaterial, this);
		Fill->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(.05f, 1.f, .2f));
		ProgressFill->SetMaterial(0, Fill);
	}
	GetWorld()->GetSubsystem<ULSMineHazardSubsystem>()->RegisterMine(this);
	TArray<UPrimitiveComponent*> Overlaps;
	ContactSphere->GetOverlappingComponents(Overlaps);
	for (UPrimitiveComponent* Component : Overlaps)
	{
		OnContact(ContactSphere, Component->GetOwner(), Component, 0, false, FHitResult());
		if (!IsArmed())
			break;
	}
}
void ALSMine::NotifyChanged()
{
	if (GetWorld())
	{
		GetWorld()->GetSubsystem<ULSMineHazardSubsystem>()->NotifyMineChanged(this);
		GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
	}
}
bool ALSMine::HasDisarmWorker() const
{
	return DisarmWorker.IsValid();
}
bool ALSMine::TryClaimDisarm(AActor* Worker)
{
	const ALSUnit* Unit = Cast<ALSUnit>(Worker);
	if (!IsArmed() || HasDisarmWorker() || !Unit || Unit->Selection->Kind != ELSSelectionKind::Unit ||
		Unit->GetFaction() != ELSFaction::Ally || !ULSCombatTargetLibrary::IsCombatAlive(Unit))
		return false;
	DisarmWorker = Worker;
	SetDisarmProgress(Worker, 0);
	NotifyChanged();
	return true;
}
void ALSMine::ReleaseDisarm(AActor* Worker)
{
	if (DisarmWorker.Get() != Worker)
		return;
	DisarmWorker.Reset();
	ProgressBackground->SetVisibility(false);
	ProgressFill->SetVisibility(false);
	NotifyChanged();
}
bool ALSMine::TryCompleteDisarm(AActor* Worker)
{
	if (!IsArmed() || !IsValid(Worker) || DisarmWorker.Get() != Worker ||
		!ULSCombatTargetLibrary::IsCombatAlive(Worker))
		return false;
	FinishMine(ELSMineState::Disarmed);
	return true;
}
void ALSMine::SetDisarmProgress(AActor* Worker, float Progress)
{
	if (!IsArmed() || !IsValid(Worker) || DisarmWorker.Get() != Worker || !FMath::IsFinite(Progress))
		return;
	ProgressBackground->SetVisibility(true);
	ProgressFill->SetVisibility(true);
	const float Fraction = FMath::Clamp(Progress, 0.f, 1.f);
	ProgressFill->SetRelativeScale3D(FVector(FMath::Max(.001f, Fraction), .1, .05));
	ProgressFill->SetRelativeLocation(FVector((Fraction - 1) * 50, 0, 86));
}
void ALSMine::SetHovered(bool bHovered)
{
	bIsHovered = bHovered && IsHazardActive();
	SetActorTickEnabled(bIsHovered || IsTriggered());
}
void ALSMine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsHazardActive())
	{
		SetActorTickEnabled(false);
		return;
	}
	if (bIsHovered)
	{
		const FVector Center = GetTriggerCenter() + FVector(0, 0, 2);
		DrawDebugCircle(GetWorld(), Center, GetTriggerRadius(), 48, FColor::Yellow, false, -1, 0, 3,
						FVector::ForwardVector, FVector::RightVector, false);
		DrawDebugCircle(GetWorld(), Center, ExplosionRadius, 64, FColor::Red, false, -1, 0, 2, FVector::ForwardVector,
						FVector::RightVector, false);
	}
	if (IsTriggered())
	{
		const float Pulse = .5f + .5f * FMath::Sin(static_cast<float>(GetWorld()->GetTimeSeconds() - TriggeredAt) * 80);
		if (MineMaterial)
			MineMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(1, .05f + Pulse * .7f, 0));
		DrawDebugPoint(GetWorld(), GetInteractionLocation() + FVector(0, 0, 45), 16, FColor::Orange, false, -1);
	}
}
void ALSMine::OnContact(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
						int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
	if (!IsArmed() || !IsValid(Other) || !IsValid(OtherComponent) || OtherComponent != Other->GetRootComponent())
		return;
	const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Other);
	const bool bSupported = Other->IsA<ALSCastle>() || Other->IsA<ALSMobileCastle>() || Other->IsA<ALSUnit>();
	if (bSupported && Faction && Faction->GetFaction() == ELSFaction::Ally &&
		ULSCombatTargetLibrary::IsCombatAlive(Other))
		RequestTrigger(ELSMineTriggerCause::Contact);
}
void ALSMine::OnDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage > 0)
		RequestTrigger(ELSMineTriggerCause::DirectDamage);
}
void ALSMine::PrepareForDeath()
{
	RequestTrigger(ELSMineTriggerCause::DirectDamage);
}
bool ALSMine::RequestTrigger(ELSMineTriggerCause Cause)
{
	if (!IsArmed() || !GetWorld() || GetWorld()->bIsTearingDown)
		return false;
	State = ELSMineState::Triggered;
	TriggerCause = Cause;
	TriggeredAt = GetWorld()->GetTimeSeconds();
	SetActorTickEnabled(true);
	if (MineMaterial)
		MineMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(1, .8f, 0));
	DisarmWorker.Reset();
	ProgressBackground->SetVisibility(false);
	ProgressFill->SetVisibility(false);
	NotifyChanged();
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
		return true;
	if (Cause == ELSMineTriggerCause::Chain)
		GetWorld()->GetTimerManager().SetTimer(ChainTimer, this, &ALSMine::Explode, ChainDelay, false);
	else
		Explode();
	return true;
}
bool ALSMine::HasBlastLine(UPrimitiveComponent* Body) const
{
	FVector Closest;
	if (!IsValid(Body) || Body->GetClosestPointOnCollision(GetTriggerCenter(), Closest) < 0)
		return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSMineBlast), false, this);
	Query.AddIgnoredActor(Body->GetOwner());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, GetTriggerCenter(), Closest,
												 LSCollisionChannels::ExplosionOcclusion, Query);
}
void ALSMine::Explode()
{
	if (!IsTriggered() || !GetWorld() || GetWorld()->bIsTearingDown)
		return;
	State = ELSMineState::Exploding;
	NotifyChanged();
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
		return;
	DrawDebugSphere(GetWorld(), GetTriggerCenter(), ExplosionRadius, 24, FColor::Orange, false, .3f, 0, 3);
	DrawDebugPoint(GetWorld(), GetTriggerCenter(), 28, FColor::Red, false, .3f);
	TArray<TWeakObjectPtr<AActor>> Targets;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		Targets.Add(*It);
	for (const TWeakObjectPtr<AActor>& Weak : Targets)
	{
		if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
			return;
		AActor* Target = Weak.Get();
		if (!IsValid(Target) || Target == this)
			continue;
		if (ALSMine* Mine = Cast<ALSMine>(Target))
		{
			if (Mine->IsArmed() &&
				FVector::Dist(GetTriggerCenter(), Mine->GetTriggerCenter()) <=
					ExplosionRadius + Mine->GetTriggerRadius() &&
				HasBlastLine(Mine->ContactSphere))
				Mine->RequestTrigger(ELSMineTriggerCause::Chain);
			continue;
		}
		const bool bCastle = Target->IsA<ALSCastle>() || Target->IsA<ALSMobileCastle>();
		const ALSUnit* Unit = Cast<ALSUnit>(Target);
		if (!bCastle && (!Unit || Unit->Selection->Kind != ELSSelectionKind::Unit))
			continue;
		UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Target->GetRootComponent());
		FVector Closest;
		if (!Body || Body->GetClosestPointOnCollision(GetTriggerCenter(), Closest) < 0 ||
			FVector::DistSquared(GetTriggerCenter(), Closest) > FMath::Square(ExplosionRadius) || !HasBlastLine(Body))
			continue;
		ULSHealthComponent* TargetHealth = Target->FindComponentByClass<ULSHealthComponent>();
		FLSHealthSnapshot HP;
		if (!TargetHealth || !TargetHealth->TryGetHealth(HP) || HP.bIsDead)
			continue;
		FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(
			bCastle ? HP.MaxHealth * CastleMaxHealthFraction : HP.CurrentHealth, this, this);
		Request.Policy = ELSDamagePolicy::EnvironmentalHazard;
		TargetHealth->ApplyDamage(Request);
		if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
			return;
	}
	FinishMine(ELSMineState::Spent);
}
void ALSMine::FinishMine(ELSMineState FinalState)
{
	State = FinalState;
	DisarmWorker.Reset();
	bIsHovered = false;
	SetActorTickEnabled(false);
	if (FinalState == ELSMineState::Disarmed)
	{
		DrawDebugSphere(GetWorld(), GetTriggerCenter(), 55, 16, FColor::Green, false, .6f, 0, 3);
		DrawDebugPoint(GetWorld(), GetTriggerCenter(), 18, FColor::Green, false, .6f);
	}
	GetWorld()->GetTimerManager().ClearTimer(ChainTimer);
	DisableObstacle();
	if (!IsValid(this) || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown)
		return;
	ProgressBackground->SetVisibility(false);
	ProgressFill->SetVisibility(false);
	GetWorld()->GetSubsystem<ULSMineHazardSubsystem>()->UnregisterMine(this);
	SetActorHiddenInGame(true);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }));
}
void ALSMine::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ChainTimer);
		GetWorld()->GetSubsystem<ULSMineHazardSubsystem>()->UnregisterMine(this);
	}
	Super::EndPlay(Reason);
}
