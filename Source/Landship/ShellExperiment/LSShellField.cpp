#include "LSShellField.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellReactiveComponent.h"
#include "LSShellObstacleResponseComponent.h"
#include "LSShellSiegeController.h"
#include "LSShellWeakPoint.h"
#include "LSShellMaterials.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
bool ArenaSurface(ULSShellReactiveComponent* Reactive, FVector Center, FVector& Surface, double& Distance)
{
	if (!Reactive || !Reactive->CanReceiveMomentum()) return false;
	UPrimitiveComponent* Body = Reactive->GetPhysicalBody();
	Center.Z = Body->IsSimulatingPhysics() ? Body->GetCenterOfMass().Z : Body->Bounds.Origin.Z;
	double Ignored;
	if (!LSObstacleGeometry::GetClosestBodyPoint(Body, Center, Surface, Ignored)) return false;
	Distance = FVector::Dist2D(Center, Surface);
	return true;
}

bool ArenaAnchored(const TArray<UPrimitiveComponent*>& Assembly)
{
	for (const UPrimitiveComponent* Body : Assembly)
		if (const auto* Plate = Cast<ALSShellPlate>(Body->GetOwner()); Plate && Plate->IsSupported()) return true;
	return false;
}

bool ArenaExposed(UWorld* World, AActor* Field, ULSShellReactiveComponent* Reactive, FVector Center, FVector Surface)
{
	Center.Z = Surface.Z = Reactive->GetPhysicalBody()->IsSimulatingPhysics()
		? Reactive->GetPhysicalBody()->GetCenterOfMass().Z : Reactive->GetPhysicalBody()->Bounds.Origin.Z;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ShellArenaGravityCover), false, Field);
	Query.AddIgnoredActor(Reactive->GetOwner());
	for (TActorIterator<ALSShellWeakPoint> It(World); It; ++It) Query.AddIgnoredActor(*It);
	// 이동 기물도 Building 채널을 쓴다. 고정 차폐와 가동 몸체를 채널만으로 구분하지 않는다.
	for (TActorIterator<AActor> It(World); It; ++It)
		if (auto* Other = It->FindComponentByClass<ULSShellReactiveComponent>(); Other && Other->CanReceiveMomentum() &&
			!ArenaAnchored(ULSShellObstacleResponseComponent::CollectStructuralBodies(Other->GetPhysicalBody()))) Query.AddIgnoredActor(*It);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, Center, Surface, Objects, Query);
	for (const FHitResult& Hit : Hits)
		if (Hit.GetComponent() && (Hit.GetComponent()->GetCollisionObjectType() == LSCollisionChannels::Building ||
			Hit.GetComponent()->Mobility != EComponentMobility::Movable)) return false;
	return true;
}

UMaterialInstanceDynamic* ApplyFieldMaterial(UStaticMeshComponent* Visual, UObject* Owner, FLinearColor Color, float Opacity, bool bRing = false)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, bRing
		? TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellRing.M_ShellRing")
		: TEXT("/Game/Landship/ShellExperiment/Materials/M_ShellEffect.M_ShellEffect"));
	if (!Base) return nullptr;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Owner);
	Material->SetVectorParameterValue(TEXT("Tint"), Color);
	Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	Visual->SetMaterial(0, Material);
	return Material;
}
}

ALSShellVisualPulse::ALSShellVisualPulse()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pulse"));
	Visual->SetupAttachment(RootComponent);
	Flash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flash"));
	Flash->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Visual->SetStaticMesh(Sphere.Object);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false);
	Flash->SetStaticMesh(Sphere.Object);
	Flash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flash->SetCanEverAffectNavigation(false);
	Flash->SetCastShadow(false);
}

void ALSShellVisualPulse::Initialize(float Radius, FLinearColor Color, float Seconds)
{
	TargetRadius = Radius;
	Duration = FMath::Max(0.01f, Seconds);
	Material = ApplyFieldMaterial(Visual, this, Color * 1.5f, 0.4f, true);
	FlashMaterial = ApplyFieldMaterial(Flash, this, Color * 3, 0.9f);
	Visual->SetRelativeScale3D(FVector(0.1f * Radius / 50, 0.1f * Radius / 50, 0.1f));
	Flash->SetRelativeScale3D(FVector(0.5f));
}

void ALSShellVisualPulse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
	const float WaveScale = FMath::Lerp(0.1f, 1.f, FMath::Sqrt(T)) * TargetRadius / 50;
	Visual->SetRelativeScale3D(FVector(WaveScale, WaveScale, 0.1f));
	const float FlashT = FMath::Clamp(Age / 0.12f, 0.f, 1.f);
	Flash->SetRelativeScale3D(FVector(FMath::Lerp(0.5f, 2.f, FlashT)));
	Flash->SetVisibility(FlashT < 1);
	if (FlashMaterial) FlashMaterial->SetScalarParameterValue(TEXT("Opacity"), (1 - FlashT) * 0.9f);
	if (Material) Material->SetScalarParameterValue(TEXT("Opacity"), (1 - T) * 0.4f);
	if (T >= 1) Destroy();
}

ALSShellGravityField::ALSShellGravityField()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gravity"));
	Visual->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Visual->SetStaticMesh(Sphere.Object);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false);
}

void ALSShellGravityField::Initialize(ALSShellExperimentCoordinator* InCoordinator, float Radius, float Seconds, float Acceleration)
{
	Coordinator = InCoordinator;
	FieldRadius = Radius;
	Remaining = Seconds;
	PullAcceleration = Acceleration;
	VisualAge = 0;
	Visual->SetRelativeScale3D(FVector(Radius / 50, Radius / 50, 0.15f) * 0.03f);
	Material = ApplyFieldMaterial(Visual, this, FLinearColor(0.1f, 0.55f, 3), 0.7f, true);
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShellGravityVisualGround), false, this);
	if (GetWorld()->LineTraceSingleByObjectType(Ground, GetActorLocation() + FVector(0, 0, 100),
		GetActorLocation() - FVector(0, 0, 3000), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		Visual->SetWorldLocation(Ground.ImpactPoint + FVector(0, 0, 12));
	// 컴포넌트의 속도 표본은 중력 합산 뒤, CharacterMovement 직전에 채취한다.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!ULSCombatTargetLibrary::IsCombatAlive(*It) || FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) > FieldRadius) continue;
		if (ULSShellReactiveComponent* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>())
			RegisterGravityPrerequisite(Reactive);
	}
}

void ALSShellGravityField::RegisterGravityPrerequisite(ULSShellReactiveComponent* Reactive)
{
	if (GravityPrerequisites.Contains(Reactive)) return;
	Reactive->AddTickPrerequisiteActor(this);
	GravityPrerequisites.Add(Reactive);
}

void ALSShellGravityField::InitializeSupply(ALSShellExperimentCoordinator* InCoordinator, const FLSShellShot& Shot)
{
	bArenaGather = LSShellUsesArena(Shot);
	ArenaTuning = Shot.SupplyTuning.Arena;
	if (bArenaGather) SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, 0));
	const float Radius = bArenaGather && Shot.GatherRadiusOverride >= 0 ? Shot.GatherRadiusOverride : Shot.BaseTuning.GravityRadius;
	const float Seconds = bArenaGather && Shot.GatherSecondsOverride >= 0 ? Shot.GatherSecondsOverride : Shot.BaseTuning.GravitySeconds;
	Initialize(InCoordinator, Radius, Seconds, 0);
	bSupplyField = true;
	SupplyMomentumRemaining = FMath::Max(0.f, Shot.SupplyTuning.GravityIntegratedMomentum * Shot.EffectScale);
	SupplyMomentumRate = Remaining > 0 ? SupplyMomentumRemaining / Remaining : 0;
	SupplyDeltaSpeedCap = FMath::Max(0.f, Shot.SupplyTuning.ModuleMaximumDeltaSpeed);
	DeliveredDeltaSpeeds.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>(); Reactive && Reactive->CanReceiveMomentum())
			RegisterGravityPrerequisite(Reactive);
}

float ALSShellGravityField::ReleaseRemainingSupplyMomentum()
{
	const float Unspent = SupplyMomentumRemaining;
	SupplyMomentumRemaining = 0;
	Destroy();
	return Unspent;
}

void ALSShellGravityField::StopGathering()
{
	for (const auto& WeakReactive : GravityPrerequisites)
		if (auto* Reactive = WeakReactive.Get()) Reactive->CancelArenaGather(this);
	Destroy();
}

FLSShellGatherBudget ALSShellGravityField::ReleaseRemainingGather()
{
	const FLSShellGatherBudget Budget{FieldRadius, FMath::Max(0.f, Remaining)};
	StopGathering();
	return Budget;
}

FLSShellGatherOutput ALSShellGravityField::CaptureGatherOutput() const
{
	FLSShellGatherOutput Output;
	Output.Center = GetActorLocation();
	TArray<ULSShellReactiveComponent*> Pending;
	TSet<ULSShellReactiveComponent*> Seen;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>();
		FVector Surface; double Distance;
		if (ArenaSurface(Reactive, Output.Center, Surface, Distance) && Distance <= ArenaTuning.GatheredSurfaceDistance &&
			ArenaExposed(GetWorld(), const_cast<ALSShellGravityField*>(this), Reactive, Output.Center, Surface)) Pending.Add(Reactive);
	}
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		auto* Reactive = Pending[Index];
		if (!Reactive || Seen.Contains(Reactive) || !Reactive->CanReceiveMomentum()) continue;
		const auto Assembly = ULSShellObstacleResponseComponent::CollectStructuralBodies(Reactive->GetPhysicalBody());
		if (ArenaAnchored(Assembly)) continue;
		for (UPrimitiveComponent* Body : Assembly)
		{
			auto* Member = Body->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
			if (!Member || Seen.Contains(Member) || !Member->CanReceiveMomentum()) continue;
			Seen.Add(Member);
			Output.Members.Add({Member, Member->BodyId, 0, Member->GetBodyGeneration(), Member->GetPhysicsMassKg(), Body->IsSimulatingPhysics() ? Body->GetCenterOfMass() : Body->Bounds.Origin});
			FVector MemberSurface; double MemberDistance;
			if (!ArenaSurface(Member, Output.Center, MemberSurface, MemberDistance) || MemberDistance > FieldRadius) continue;
			for (UPrimitiveComponent* Other : Member->GetContactBodies())
			{
				auto* Peer = IsValid(Other) && IsValid(Other->GetOwner()) ? Other->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
				FVector Surface; double Distance;
				if (Peer && Peer->GetPhysicalBody() == Other && !Seen.Contains(Peer) &&
					ArenaSurface(Peer, Output.Center, Surface, Distance) && Distance <= FieldRadius) Pending.Add(Peer);
			}
		}
	}
	return Output;
}

void ALSShellGravityField::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const TWeakObjectPtr<ULSShellReactiveComponent>& WeakReactive : GravityPrerequisites)
		if (ULSShellReactiveComponent* Reactive = WeakReactive.Get())
		{
			Reactive->CancelArenaGather(this);
			Reactive->RemoveTickPrerequisiteActor(this);
		}
	GravityPrerequisites.Reset();
	Super::EndPlay(Reason);
}

void ALSShellGravityField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Coordinator.IsValid() || !Coordinator->IsBattleActive() || Remaining <= 0) { Destroy(); return; }
	VisualAge = FMath::Min(VisualAge + DeltaSeconds, 0.3f);
	const float GrowT = VisualAge / 0.3f;
	const float VisualScale = FMath::Lerp(0.03f, 1.f, GrowT * GrowT * (3 - 2 * GrowT));
	Visual->SetRelativeScale3D(FVector(FieldRadius / 50, FieldRadius / 50, 0.15f) * VisualScale);
	const float Step = FMath::Min(Remaining, DeltaSeconds);
	int32 TetherIndex = 0;
	TSet<TWeakObjectPtr<ULSShellReactiveComponent>> CurrentReactives;
	if (bArenaGather)
	{
		TSet<UPrimitiveComponent*> Visited;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>();
			if (!Reactive || !Reactive->CanReceiveMomentum() || Visited.Contains(Reactive->GetPhysicalBody())) continue;
			const auto Assembly = ULSShellObstacleResponseComponent::CollectStructuralBodies(Reactive->GetPhysicalBody());
			for (auto* Body : Assembly) Visited.Add(Body);
			if (ArenaAnchored(Assembly)) continue;
			float EffectiveMass = 0, ActualMass = 0;
			FVector AssemblyVelocity = FVector::ZeroVector;
			double ClosestDistance = TNumericLimits<double>::Max();
			FVector ClosestSurface = FVector::ZeroVector;
			for (auto* Body : Assembly)
			{
				auto* Member = Body->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
				if (!Member || !Member->CanReceiveMomentum()) continue;
				const float Mass = Member->GetPhysicsMassKg();
				ActualMass += Mass;
				AssemblyVelocity += Member->GetMomentumAdjustedLinearVelocity() * Mass;
				const auto* Siege = Body->GetOwner()->FindComponentByClass<ULSShellSiegeController>();
				EffectiveMass += Member->GetPhysicsMassKg() * FMath::Max(KINDA_SMALL_NUMBER, Member->GetMovementResistance()) *
					(Siege ? Siege->GetResponseResistanceMultiplier() : 1.f);
				FVector Surface; double Distance;
				if (ArenaSurface(Member, GetActorLocation(), Surface, Distance) && Distance < ClosestDistance &&
					ArenaExposed(GetWorld(), this, Member, GetActorLocation(), Surface))
				{ ClosestDistance = Distance; ClosestSurface = Surface; }
			}
			if (EffectiveMass <= 0 || ActualMass <= 0 || ClosestDistance >= FieldRadius) continue;
			AssemblyVelocity /= ActualMass;
			const float MaxSpeed = FMath::Clamp(ArenaTuning.GravityReferenceSpeed * FMath::Pow(100.f / EffectiveMass,
				ArenaTuning.GravityMassExponent), ArenaTuning.GravityMinimumSpeed, ArenaTuning.GravityMaximumSpeed);
			const FVector Target = (GetActorLocation() - ClosestSurface).GetSafeNormal2D() *
				FMath::Min(MaxSpeed, ArenaTuning.GravityPositionGain * static_cast<float>(ClosestDistance));
			const float Weight = 1 - ClosestDistance / FieldRadius;
			for (auto* Body : Assembly)
			{
				auto* Member = Body->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
				if (!Member || !Member->CanReceiveMomentum()) continue;
				RegisterGravityPrerequisite(Member);
				CurrentReactives.Add(Member);
				Member->QueueArenaGather(this, Target, AssemblyVelocity, Weight, ArenaTuning.GravityResponseSeconds,
					ArenaTuning.GravityMaximumAcceleration, MaxSpeed, Step);
			}
		}
	}
	else if (bSupplyField)
	{
		struct FLSSupplyGravityTarget { ULSShellReactiveComponent* Reactive; FVector Direction; float Weight; };
		TArray<FLSSupplyGravityTarget> Targets;
		float SumWeights = 0;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>();
			if (!Reactive || !Reactive->CanReceiveMomentum()) continue;
			FVector Surface;
			double Distance;
			if (!LSObstacleGeometry::GetClosestBodyPoint(Reactive->GetPhysicalBody(), GetActorLocation(), Surface, Distance) ||
				Distance >= FieldRadius) continue;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellGravityExposure), false, this);
			Query.AddIgnoredActor(Reactive->GetOwner());
			FHitResult Cover;
			if (GetWorld()->LineTraceSingleByChannel(Cover, GetActorLocation(), Surface,
				LSCollisionChannels::ExplosionOcclusion, Query)) continue;
			const float Weight = 1 - Distance / FieldRadius;
			const FVector Center = Reactive->GetPhysicalBody()->IsSimulatingPhysics()
				? Reactive->GetPhysicalBody()->GetCenterOfMass() : Reactive->GetPhysicalBody()->Bounds.Origin;
			Targets.Add({Reactive, (GetActorLocation() - Center).GetSafeNormal(), Weight});
			SumWeights += Weight;
			CurrentReactives.Add(Reactive);
			RegisterGravityPrerequisite(Reactive);
		}
		const float StepBudget = FMath::Min(SupplyMomentumRemaining, SupplyMomentumRate * Step);
		// 상한에 걸린 몫과 빈 공간에 쓴 시간은 다른 몸체나 다음 시간에 재분배하지 않는다.
		for (const FLSSupplyGravityTarget& Target : Targets)
		{
			const float MassKg = Target.Reactive->GetPhysicsMassKg();
			if (MassKg <= 0) continue;
			float& Delivered = DeliveredDeltaSpeeds.FindOrAdd(Target.Reactive->BodyId);
			const float Impulse = FMath::Min(StepBudget * Target.Weight / FMath::Max(1.f, SumWeights),
				MassKg * FMath::Max(0.f, SupplyDeltaSpeedCap - Delivered));
			if (Target.Reactive->ApplyMomentum(Target.Direction * Impulse))
			{
				Delivered += Impulse / MassKg;
				Target.Reactive->NotifyExternalForce();
			}
		}
		SupplyMomentumRemaining = FMath::Max(0.f, SupplyMomentumRemaining - StepBudget);
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (ULSShellReactiveComponent* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>())
		{
			if (bSupplyField ? !CurrentReactives.Contains(Reactive) :
				(!ULSCombatTargetLibrary::IsCombatAlive(*It) || FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) > FieldRadius)) continue;
			RegisterGravityPrerequisite(Reactive);
			CurrentReactives.Add(Reactive);
			if (!bSupplyField) Reactive->ApplyGravity(GetActorLocation(), PullAcceleration, Step);
			if (!Reactive->CanMoveByGravity()) continue;
			if (!Tethers.IsValidIndex(TetherIndex))
			{
				UStaticMeshComponent* Tether = NewObject<UStaticMeshComponent>(this);
				AddInstanceComponent(Tether);
				Tether->SetupAttachment(RootComponent);
				// SetStaticMesh는 등록 전에도 nav 요소를 만들 수 있다. 표현용 설정을 먼저 적용한다.
				Tether->SetCanEverAffectNavigation(false);
				Tether->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Tether->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
				Tether->SetCastShadow(false);
				if (!TetherMaterial) TetherMaterial = ApplyFieldMaterial(Tether, this, FLinearColor(0.08f, 0.5f, 2), 0.5f);
				else Tether->SetMaterial(0, TetherMaterial);
				Tether->RegisterComponent();
				Tethers.Add(Tether);
			}
			UStaticMeshComponent* Tether = Tethers[TetherIndex++];
			const FVector Start = Visual->GetComponentLocation() + FVector(0, 0, 25);
			const FVector End = It->GetActorLocation();
			const FVector Delta = End - Start;
			Tether->SetWorldLocation((Start + End) * 0.5f);
			Tether->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta).Rotator());
			Tether->SetWorldScale3D(FVector(0.035f, 0.035f, Delta.Size() / 100));
			Tether->SetVisibility(true);
		}
	}
	for (auto It = GravityPrerequisites.CreateIterator(); It; ++It)
	{
		if (It->IsValid() && CurrentReactives.Contains(*It)) continue;
		if (ULSShellReactiveComponent* Reactive = It->Get()) Reactive->RemoveTickPrerequisiteActor(this);
		It.RemoveCurrent();
	}
	for (; TetherIndex < Tethers.Num(); ++TetherIndex) Tethers[TetherIndex]->SetVisibility(false);
	Remaining -= DeltaSeconds;
	if (!bArenaGather && Remaining <= 0) Destroy();
}
