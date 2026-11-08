#include "LSShellEffectInstance.h"
#include "LSShellExperimentCoordinator.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "TimerManager.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "LSShellReactiveComponent.h"
#include "LSShellField.h"
#include "LSShellSplitBudget.h"

ALSShellEffectInstance::ALSShellEffectInstance()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EffectAnchor")));
}

ALSShellGravityField* ALSShellEffectInstance::GetSourceGravityField() const { return SourceGravityField.Get(); }
void ALSShellEffectInstance::SetSourceGravityField(ALSShellGravityField* Field) { SourceGravityField = Field; }

void ALSShellEffectInstance::Initialize(ALSShellExperimentCoordinator* InCoordinator, const FLSShellShot& InShot, const FHitResult& Hit, bool bInCombatImpact, AActor* BodyTarget)
{
	Coordinator = InCoordinator;
	Shot = InShot;
	if (LSShellUsesArena(Shot))
	{
		if (Shot.First == ELSShellModule::Gravity && (Shot.Second == ELSShellModule::Blast ||
			Shot.Second == ELSShellModule::Cooling || Shot.Second == ELSShellModule::Thrust))
			FollowupBinding = EFollowupBinding::GatherOutput;
		else if (Shot.Second == ELSShellModule::Cooling) FollowupBinding = EFollowupBinding::LastPoint;
	}
	Impact = Hit;
	InitialImpactPoint = Hit.ImpactPoint;
	bCombatImpact = bInCombatImpact;
	SetActorLocation(Hit.ImpactPoint);
	AActor* Target = BodyTarget ? BodyTarget : Hit.GetActor();
	const auto* Body = IsValid(Target) ? Target->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
	if (ULSCombatTargetLibrary::IsCombatAlive(Target) || (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Body && Body->IsPhysicalBodyPresent()))
	{
		Anchor = Target;
		LastAnchorOwner = Target;
		if (Body) { AnchorBodyId = Body->BodyId; AnchorGeneration = Body->GetBodyGeneration(); }
		LocalPoint = Target->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
		LocalNormal = Target->GetActorTransform().InverseTransformVectorNoScale(Hit.ImpactNormal);
		if (auto* Health = Target->FindComponentByClass<ULSHealthComponent>())
			Health->OnDiedEvent.AddDynamic(this, &ThisClass::OnAnchorDied);
		if (BodyTarget && BodyTarget != Hit.GetActor())
		{
			Impact.HitObjectHandle = FActorInstanceHandle(Target);
			Impact.Component = Body ? Body->GetPhysicalBody() : Cast<UPrimitiveComponent>(Target->GetRootComponent());
		}
	}
	FLSShellShot FirstShot = Shot;
	if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.Second == ELSShellModule::Split && !Shot.bChild)
	{
		const float DirectDamage = LSShellUsesArena(Shot) && Shot.First == ELSShellModule::Cooling ? 0 : Shot.BaseTuning.DirectDamage;
		const auto Budget = FLSShellSplitBudget::Allocate(Shot.First, ELSShellSplitOrder::SplitSecond, DirectDamage);
		FirstShot.EffectScale = Budget.Parent.EffectScale;
		FirstShot.AdhesionSlots = Budget.Parent.AdhesionSlots;
		FirstShot.ColdSeconds = Budget.Parent.ColdSeconds;
	}
	InCoordinator->ExecuteModule(Shot.First, GetActorLocation(), FirstShot, Impact, bCombatImpact, this);
	// 생성 실패는 대상 소멸과 다르다. 원래 몸체나 착탄점으로 후행 효과를 대체하지 않는다.
	if (bGeneratedBodyFailed) { Destroy(); return; }
	if (Shot.bChild || Shot.First == ELSShellModule::Split) { Destroy(); return; }
	bPending = true;
	InCoordinator->SetManualTarget(this);
	const float Seconds = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne ? LSShellFollowupDelay(Shot) : InCoordinator->Tuning.ModuleDelay;
	GetWorldTimerManager().SetTimer(Delay, this, &ThisClass::ActivateSecond, Seconds, false);
}

void ALSShellEffectInstance::CancelForBodyRemoval(AActor* Target)
{
	if (FollowupBinding == EFollowupBinding::ExistingAnchor && (Anchor.Get() == Target || LastAnchorOwner.Get() == Target)) Destroy();
}

void ALSShellEffectInstance::RemapAnchor(AActor* Previous, AActor* Replacement)
{
	if ((Anchor.Get() != Previous && LastAnchorOwner.Get() != Previous) || !IsValid(Replacement)) return;
	const auto* Body = Replacement->FindComponentByClass<ULSShellReactiveComponent>();
	if (!Body || !Body->IsPhysicalBodyPresent() || Body->BodyId != AnchorBodyId) return;
	const FVector Point = Previous->GetActorTransform().TransformPosition(LocalPoint);
	const FVector Normal = Previous->GetActorTransform().TransformVectorNoScale(LocalNormal);
	if (auto* Health = Previous->FindComponentByClass<ULSHealthComponent>()) Health->OnDiedEvent.RemoveAll(this);
	Anchor = Replacement;
	LastAnchorOwner = Replacement;
	AnchorGeneration = Body->GetBodyGeneration();
	LocalPoint = Replacement->GetActorTransform().InverseTransformPosition(Point);
	LocalNormal = Replacement->GetActorTransform().InverseTransformVectorNoScale(Normal);
	Impact.HitObjectHandle = FActorInstanceHandle(Replacement);
	Impact.Component = Body->GetPhysicalBody();
}

void ALSShellEffectInstance::RetargetGeneratedBody(AActor* Generated)
{
	if (!IsValid(Generated)) return;
	const auto* Body = Generated->FindComponentByClass<ULSShellReactiveComponent>();
	if (!Body || !Body->IsPhysicalBodyPresent()) return;
	if (Anchor.IsValid())
		if (auto* Health = Anchor->FindComponentByClass<ULSHealthComponent>()) Health->OnDiedEvent.RemoveAll(this);
	Anchor = Generated;
	LastAnchorOwner = Generated;
	AnchorBodyId = Body->BodyId;
	AnchorGeneration = Body->GetBodyGeneration();
	FVector Point = Generated->GetActorLocation();
	Body->GetPhysicalBody()->GetClosestPointOnCollision(InitialImpactPoint, Point);
	const FVector Normal = (Point - Generated->GetActorLocation()).GetSafeNormal();
	SetActorLocation(Point);
	LocalPoint = Generated->GetActorTransform().InverseTransformPosition(Point);
	LocalNormal = Generated->GetActorTransform().InverseTransformVectorNoScale(Normal);
	Impact.HitObjectHandle = FActorInstanceHandle(Generated);
	Impact.Component = Body->GetPhysicalBody();
	Impact.ImpactPoint = Impact.Location = Point;
	Impact.ImpactNormal = Normal;
	if (auto* Health = Generated->FindComponentByClass<ULSHealthComponent>()) Health->OnDiedEvent.AddUniqueDynamic(this, &ThisClass::OnAnchorDied);
}

void ALSShellEffectInstance::UpdateAnchor()
{
	if (FollowupBinding == EFollowupBinding::GatherOutput)
	{
		if (SourceGravityField.IsValid())
			SetActorLocation(SourceGravityField->GetActorLocation() + FVector(0, 0, Shot.SupplyTuning.Arena.GatherFollowupHeight));
		return;
	}
	if (Anchor.IsValid())
	{
		if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && AnchorBodyId)
		{
			const auto* Body = Anchor->FindComponentByClass<ULSShellReactiveComponent>();
			if (!Body || Body->BodyId != AnchorBodyId || Body->GetBodyGeneration() != AnchorGeneration || !Body->IsPhysicalBodyPresent())
			{ Anchor.Reset(); UpdateAnchor(); return; }
		}
		SetActorLocation(Anchor->GetActorTransform().TransformPosition(LocalPoint));
		Impact.ImpactNormal = Anchor->GetActorTransform().TransformVectorNoScale(LocalNormal);
	}
	else if (FollowupBinding == EFollowupBinding::ExistingAnchor && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.Second == ELSShellModule::Thrust)
	{
		// 사라진 첫 대상의 접점 대신 처음 착탄한 고정 지점에서 후행 추진을 예약한다.
		SetActorLocation(InitialImpactPoint);
		Impact = FHitResult();
	}
	Impact.ImpactPoint = Impact.Location = GetActorLocation();
}

void ALSShellEffectInstance::OnAnchorDied(const FLSDamageResult& Damage)
{
	if (FollowupBinding == EFollowupBinding::GatherOutput) return;
	if (Anchor.IsValid())
	{
		SetActorLocation(Anchor->GetActorTransform().TransformPosition(LocalPoint));
		Impact.ImpactNormal = Anchor->GetActorTransform().TransformVectorNoScale(LocalNormal);
	}
	UpdateAnchor();
	if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Anchor.IsValid())
		if (const auto* Body = Anchor->FindComponentByClass<ULSShellReactiveComponent>(); Body && Body->IsPhysicalBodyPresent()) return;
	Anchor.Reset();
	UpdateAnchor();
}

void ALSShellEffectInstance::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Coordinator.IsValid() || !Coordinator->IsBattleActive()) { Destroy(); return; }
	UpdateAnchor();
	if (bPending && FollowupBinding == EFollowupBinding::ExistingAnchor && Anchor.IsValid() && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.Second == ELSShellModule::Thrust)
	{
		const auto* Body = Anchor.IsValid() ? Anchor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
		if (!Body || !Body->CanReceiveMomentum())
		{
			Coordinator->NotifyMissingTarget();
			Destroy();
		}
	}
	if (bPending && FollowupBinding == EFollowupBinding::ExistingAnchor && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne &&
		(Shot.Second == ELSShellModule::Electric || Shot.Second == ELSShellModule::Adhesion || Shot.Second == ELSShellModule::Cooling))
	{
		const auto* Body = Anchor.IsValid() ? Anchor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
		if (!Body || !Body->IsPhysicalBodyPresent())
		{
			if (Shot.Second == ELSShellModule::Electric && LSShellUsesArena(Shot))
				Coordinator->NotifyChargeFailure(GetActorLocation(), TEXT("충전 대상 소멸"));
			else Coordinator->NotifyMissingTarget();
			Destroy();
		}
	}
}

void ALSShellEffectInstance::ActivateSecond()
{
	if (!bPending) return;
	bPending = false;
	GetWorldTimerManager().ClearTimer(Delay);
	UpdateAnchor();
	if (Coordinator.IsValid())
	{
		Coordinator->ClearManualTarget(this);
		if (Coordinator->IsBattleActive())
		{
			if (LSShellUsesArena(Shot) && Shot.First == ELSShellModule::Gravity && Shot.Second != ELSShellModule::Split &&
				FollowupBinding != EFollowupBinding::GatherOutput && SourceGravityField.IsValid())
				SourceGravityField->StopGathering();
			if (FollowupBinding == EFollowupBinding::GatherOutput)
			{
				if (SourceGravityField.IsValid())
				{
					const FLSShellGatherOutput Output = SourceGravityField->CaptureGatherOutput();
					SourceGravityField->StopGathering();
					Coordinator->QueueGatherFollowup(Shot.Second, Shot, Output);
				}
				else Coordinator->NotifyMissingTarget();
			}
			else if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.Second == ELSShellModule::Thrust && !Anchor.IsValid())
				Coordinator->QueuePointThrust(InitialImpactPoint, Shot);
			else Coordinator->ExecuteModule(Shot.Second, GetActorLocation(), Shot, Impact, bCombatImpact, this);
		}
	}
	Destroy();
}

float ALSShellEffectInstance::GetPendingSeconds() const
{
	return bPending ? FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(Delay)) : 0;
}

void ALSShellEffectInstance::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(Delay);
	if (Coordinator.IsValid()) Coordinator->ClearManualTarget(this);
	Super::EndPlay(Reason);
}
