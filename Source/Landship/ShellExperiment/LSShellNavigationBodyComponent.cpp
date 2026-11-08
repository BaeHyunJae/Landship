#include "ShellExperiment/LSShellNavigationBodyComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

ULSShellNavigationBodyComponent::ULSShellNavigationBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetCanEverAffectNavigation(false);
}

void ULSShellNavigationBodyComponent::SetNavigationBody(UPrimitiveComponent* InBody)
{
	if (Body.Get() == InBody) return;
	SetNavigationRegistered(false);
	if (Body.IsValid()) Body->OnComponentWake.RemoveDynamic(this, &ThisClass::OnBodyWake);
	Body = InBody;
	StableSeconds = 0;
	if (Body.IsValid())
	{
		Body->SetCanEverAffectNavigation(false);
		NavigationReferenceTransform = Body->GetComponentTransform();
		SettleWindowOrigin = Body->GetComponentLocation();
		BindWakeEvent();
	}
}

void ULSShellNavigationBodyComponent::BindWakeEvent()
{
	if (!Body.IsValid()) return;
	Body->BodyInstance.bGenerateWakeEvents = true;
	Body->OnComponentWake.AddUniqueDynamic(this, &ThisClass::OnBodyWake);
	bWasAwake = Body->IsSimulatingPhysics() && Body->IsAnyRigidBodyAwake();
}

void ULSShellNavigationBodyComponent::SetTrackingEnabled(bool bEnabled)
{
	if (bTrackingEnabled == bEnabled) return;
	bTrackingEnabled = bEnabled;
	StableSeconds = 0;
	SetNavigationRegistered(false);
	if (Body.IsValid())
	{
		NavigationReferenceTransform = Body->GetComponentTransform();
		SettleWindowOrigin = Body->GetComponentLocation();
	}
}

UPrimitiveComponent* ULSShellNavigationBodyComponent::GetPhysicalBlockingBody() const
{
	return bTrackingEnabled ? Body.Get() : nullptr;
}

void ULSShellNavigationBodyComponent::BeginPlay()
{
	Super::BeginPlay();
	SetNavigationRegistered(false);
	BindWakeEvent();
}

void ULSShellNavigationBodyComponent::SetNavigationRegistered(bool bRelevant)
{
	if (!Body.IsValid()) { bNavigationRegistered = false; return; }
	if (bNavigationRegistered == bRelevant && Body->CanEverAffectNavigation() == bRelevant) return;
	const auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	LastDirtyAreasBefore = Nav ? Nav->GetNumDirtyAreas() : -1;
	// 이 호출이 등록된 실제 primitive를 native nav octree에 추가·제거한다. 별도 AABB 형상을 만들지 않는다.
	Body->SetCanEverAffectNavigation(bRelevant);
	bNavigationRegistered = bRelevant;
	LastDirtyAreasAfter = Nav ? Nav->GetNumDirtyAreas() : -1;
	++NavigationRevision;
	UE_LOG(LogTemp, Display, TEXT("SHELL_BODY_NAV Body=%s Relevant=%d Revision=%u DirtyBefore=%d DirtyAfter=%d Location=%s"),
		*Body->GetPathName(), bRelevant, NavigationRevision, LastDirtyAreasBefore, LastDirtyAreasAfter,
		*Body->GetComponentLocation().ToCompactString());
}

void ULSShellNavigationBodyComponent::OnBodyWake(UPrimitiveComponent* WakingComponent, FName)
{
	if (WakingComponent != Body.Get()) return;
	StableSeconds = 0;
	SetNavigationRegistered(false);
	NavigationReferenceTransform = WakingComponent->GetComponentTransform();
	SettleWindowOrigin = WakingComponent->GetComponentLocation();
	bWasAwake = true;
}

void ULSShellNavigationBodyComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	if (!bTrackingEnabled || !Body.IsValid() || !Body->IsRegistered() || !Body->IsQueryCollisionEnabled() ||
		!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed())
	{
		StableSeconds = 0;
		SetNavigationRegistered(false);
		return;
	}
	const bool bSimulating = Body->IsSimulatingPhysics();
	const bool bAwake = bSimulating && Body->IsAnyRigidBodyAwake();
	const float Speed = bSimulating ? Body->GetPhysicsLinearVelocity().Size() : Body->GetComponentVelocity().Size();
	const FVector AngularVelocity = bSimulating ? Body->GetPhysicsAngularVelocityInDegrees() : FVector::ZeroVector;
	float AngularSpeed = AngularVelocity.Size();
	const auto* Capsule = Cast<UCapsuleComponent>(Body.Get());
	bool bRotated = Body->GetComponentQuat().AngularDistance(NavigationReferenceTransform.GetRotation()) > FMath::DegreesToRadians(5.f);
	// 구는 모든 회전, 일반 캡슐은 길이 축 회전이 nav 형상을 바꾸지 않는다. 이동과 wake는 별도로 검사한다.
	if (Body->IsA<USphereComponent>() ||
		(Capsule && FMath::IsNearlyEqual(Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(), UE_KINDA_SMALL_NUMBER)))
	{
		bRotated = false;
		AngularSpeed = 0;
	}
	else if (Capsule)
	{
		const FVector Axis = Body->GetUpVector();
		bRotated = FMath::Abs(FVector::DotProduct(Axis, NavigationReferenceTransform.GetRotation().GetAxisZ())) <
			FMath::Cos(FMath::DegreesToRadians(5.f));
		AngularSpeed = (AngularVelocity - Axis * FVector::DotProduct(AngularVelocity, Axis)).Size();
	}
	const bool bMoved = FVector::DistSquared(Body->GetComponentLocation(), NavigationReferenceTransform.GetLocation()) > FMath::Square(WakeMovementDistance);
	if ((bAwake && !bWasAwake) || bMoved || bRotated || Speed >= SettleLinearSpeed ||
		AngularSpeed >= SettleAngularSpeed)
	{
		StableSeconds = 0;
		SetNavigationRegistered(false);
		NavigationReferenceTransform = Body->GetComponentTransform();
		SettleWindowOrigin = Body->GetComponentLocation();
	}
	else if (!bNavigationRegistered)
	{
		// 정착 창의 원점은 마지막 nav 등록 위치와 별개다. 느린 연속 이동도 창 안에서 1cm를 넘으면 다시 잰다.
		if (StableSeconds <= 0) SettleWindowOrigin = Body->GetComponentLocation();
		if (FVector::DistSquared(Body->GetComponentLocation(), SettleWindowOrigin) > FMath::Square(SettleDisplacement))
		{
			StableSeconds = 0;
			SettleWindowOrigin = Body->GetComponentLocation();
		}
		else StableSeconds += FMath::Max(0.f, Delta);
		if (StableSeconds >= SettleSeconds)
		{
			SetNavigationRegistered(true);
			NavigationReferenceTransform = Body->GetComponentTransform();
		}
	}
	bWasAwake = bAwake;
}

void ULSShellNavigationBodyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetNavigationRegistered(false);
	if (Body.IsValid()) Body->OnComponentWake.RemoveDynamic(this, &ThisClass::OnBodyWake);
	Super::EndPlay(Reason);
}
