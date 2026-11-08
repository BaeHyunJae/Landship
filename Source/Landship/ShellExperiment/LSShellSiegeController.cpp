#include "ShellExperiment/LSShellSiegeController.h"
#include "Combat/LSBasicProjectile.h"

#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "AIController.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Maps/LSBattlefieldSettings.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

ULSShellSiegeController::ULSShellSiegeController()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool ULSShellSiegeController::BeginSiege(AActor* Target, FVector DeploymentPoint)
{
	if (Phase != ELSShellSiegePhase::Disabled) return false;
	Heavy = Cast<ALSShellExperimentEnemy>(GetOwner());
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Heavy.IsValid() || !Heavy->IsHeavy() || !Coordinator || !Coordinator->IsSupplyExperiment() ||
		!ULSCombatTargetLibrary::CanAutoAttackTarget(Heavy.Get(), Target) || !Heavy->BasicAttack->IsConfigured() ||
		!Heavy->BasicAttack->GetProjectileSettings().ProjectileClass || DeploymentPoint.ContainsNaN())
	{
		ConfigurationError = TEXT("공성 제어에는 준비된 Heavy, 공통 투사체 공격, 유효한 적 목표와 배치점이 필요하다.");
		Phase = ELSShellSiegePhase::Invalid;
		return false;
	}
	const float PositiveValues[] { DeploySeconds, InitialLoadSeconds, ReloadSeconds, DeployedResistanceMultiplier,
		ArrivalDistance, MovementRetrySeconds, MechanicalInterruptionDeltaSpeed, RecoveryMaximumSpeed,
		RecoveryStableSeconds, MaximumTiltDegrees };
	for (float Value : PositiveValues)
		if (!FMath::IsFinite(Value) || Value <= 0)
		{
			ConfigurationError = TEXT("공성 제어 수치는 유한한 양수여야 한다.");
			Phase = ELSShellSiegePhase::Invalid;
			return false;
		}
	if (!FMath::IsFinite(InitialDelaySeconds) || InitialDelaySeconds < 0 || InitialLoadSeconds < 8)
	{
		ConfigurationError = TEXT("최초 공성 예고는 8초 이상이어야 하며 시작 지연은 유한한 음이 아닌 값이어야 한다.");
		Phase = ELSShellSiegePhase::Invalid;
		return false;
	}
	if (DeploymentOffsets.IsEmpty() || DeploymentOffsets.Num() > 8)
	{
		ConfigurationError = TEXT("공성 배치 후보는 1개 이상 8개 이하여야 한다.");
		Phase = ELSShellSiegePhase::Invalid;
		return false;
	}
	for (const FVector& Offset : DeploymentOffsets)
		if (Offset.ContainsNaN())
		{
			ConfigurationError = TEXT("공성 배치 후보는 유한해야 한다.");
			Phase = ELSShellSiegePhase::Invalid;
			return false;
		}
	SiegeTarget = Target;
	DeploymentLocation = DeploymentPoint;
	if (ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Heavy.Get(), Target, DeploymentPoint) > Heavy->BasicAttack->GetAttackSettings().Range)
	{
		ConfigurationError = TEXT("공성 배치점은 공통 공격의 실제 외곽 사거리 안에 있어야 한다.");
		Phase = ELSShellSiegePhase::Invalid;
		return false;
	}
	EarliestMoveSeconds = GetWorld()->GetTimeSeconds() + InitialDelaySeconds;
	Heavy->Behavior->SetComponentTickEnabled(false);
	Heavy->Behavior->AddTickPrerequisiteComponent(this);
	AddTickPrerequisiteComponent(Heavy->Reactive);
	Heavy->BasicAttack->StopAttack();
	Heavy->BasicAttack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateUObject(this, &ThisClass::ResolveFireTarget));
	Heavy->BasicAttack->OnAttackExecutedEvent.AddUniqueDynamic(this, &ThisClass::OnAttackExecuted);
	StopControlledMovement(false);
	ObservedExternalForceFrame = Heavy->Reactive->GetLastExternalForceFrame();
	PreviousVelocity = Heavy->GetVelocity();
	PhaseLabel = NewObject<UTextRenderComponent>(GetOwner(), TEXT("SiegePhaseLabel"));
	PhaseLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PhaseLabel->SetCanEverAffectNavigation(false);
	PhaseLabel->SetupAttachment(GetOwner()->GetRootComponent());
	PhaseLabel->SetRelativeLocation(FVector(0, 0, WarningHeight));
	PhaseLabel->SetAbsolute(false, true, false);
	PhaseLabel->SetRelativeRotation(FRotator(55, 90, 0));
	PhaseLabel->SetHorizontalAlignment(EHTA_Center);
	PhaseLabel->SetWorldSize(28);
	PhaseLabel->RegisterComponent();
	SetPhase(ELSShellSiegePhase::Waiting);
	return true;
}

void ULSShellSiegeController::SetPhase(ELSShellSiegePhase NewPhase)
{
	Phase = NewPhase;
	if (Phase == ELSShellSiegePhase::Dead || Phase == ELSShellSiegePhase::Ended) bHasActiveDeployment = false;
	PhaseSeconds = 0;
	bShotCommitted = false;
	if (Phase != ELSShellSiegePhase::Firing && Heavy.IsValid()) Heavy->BasicAttack->StopAttack();
}

void ULSShellSiegeController::StopControlledMovement(bool bPreserveVelocity)
{
	if (!Heavy.IsValid()) return;
	auto* Movement = Heavy->GetCharacterMovement();
	const FVector Velocity = Movement->Velocity, PendingLaunch = Movement->PendingLaunchVelocity;
	if (auto* Controller = Cast<AAIController>(Heavy->GetController())) Controller->StopMovement();
	if (bPreserveVelocity)
	{
		Movement->Velocity = Velocity;
		Movement->PendingLaunchVelocity = PendingLaunch;
	}
}

void ULSShellSiegeController::SetExternalControlHeld(FName Reason, bool bHeld)
{
	if (Reason.IsNone()) return;
	if (bHeld) ExternalControlHolds.Add(Reason);
	else ExternalControlHolds.Remove(Reason);
}

float ULSShellSiegeController::GetResponseResistanceMultiplier() const
{
	return Phase == ELSShellSiegePhase::Loading || Phase == ELSShellSiegePhase::Firing || Phase == ELSShellSiegePhase::Reloading
		? DeployedResistanceMultiplier : 1.f;
}

AActor* ULSShellSiegeController::ResolveFireTarget() const
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (Phase != ELSShellSiegePhase::Firing || bShotCommitted || !ExternalControlHolds.IsEmpty() || !Heavy.IsValid() ||
		!Coordinator || !Coordinator->IsBattleActive() || !ULSCombatTargetLibrary::CanAutoAttackTarget(Heavy.Get(), SiegeTarget.Get())) return nullptr;
	if (Heavy->Reactive->GetLastExternalForceFrame() != ObservedExternalForceFrame || Heavy->Reactive->IsForcedMotionActive()) return nullptr;
	if (!bHasActiveDeployment || FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) > ArrivalDistance) return nullptr;
	return SiegeTarget.Get();
}

bool ULSShellSiegeController::IsDeploymentPoseUsable(FVector Candidate, bool bRequirePath, FVector& OutPose) const
{
	if (!Heavy.IsValid() || !SiegeTarget.IsValid()) return false;
	const UCapsuleComponent* Capsule = Heavy->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShellSiegeDeployment), false, Heavy.Get());
	if (!GetWorld()->LineTraceSingleByObjectType(Ground, Candidate + FVector(0, 0, 100), Candidate - FVector(0, 0, HalfHeight + 300), GroundObjects, Params) ||
		Ground.ImpactNormal.Z < .99f) return false;
	Candidate.Z = Ground.ImpactPoint.Z + HalfHeight;
	if (const auto* Battlefield = ALSBattlefieldSettings::Find(GetWorld()))
	{
		const FBox2D Bounds = Battlefield->GetWorldBounds();
		if (Candidate.X - Radius < Bounds.Min.X || Candidate.X + Radius > Bounds.Max.X || Candidate.Y - Radius < Bounds.Min.Y || Candidate.Y + Radius > Bounds.Max.Y) return false;
	}
	for (TActorIterator<ALSShellExperimentEnemy> It(GetWorld()); It; ++It)
	{
		if (*It == Heavy.Get() || !ULSCombatTargetLibrary::IsCombatAlive(*It)) continue;
		const auto* Other = It->FindComponentByClass<ULSShellSiegeController>();
		if (Other && Other->bHasActiveDeployment && FVector::DistXY(Candidate, Other->ActiveDeploymentLocation) <
			Radius + It->GetCapsuleComponent()->GetScaledCapsuleRadius() + ArrivalDistance + Other->ArrivalDistance) return false;
	}
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Candidate, Capsule->GetComponentQuat(), FCollisionObjectQueryParams::AllObjects,
		Capsule->GetCollisionShape(-UE_KINDA_SMALL_NUMBER), Params);
	for (const FOverlapResult& Overlap : Overlaps)
		if (const UPrimitiveComponent* Other = Overlap.GetComponent(); Other &&
			Other->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) == ECR_Block &&
			Capsule->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) == ECR_Block) return false;
	if (ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Heavy.Get(), SiegeTarget.Get(), Candidate) > Heavy->BasicAttack->GetAttackSettings().Range ||
		!Heavy->BasicAttack->HasClearAttackPathFromLocation(Candidate, SiegeTarget.Get())) return false;
	if (bRequirePath)
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Heavy->GetNavAgentPropertiesRef(), Heavy->GetNavAgentLocation()) : nullptr;
		FNavLocation Projected;
		if (!Data || !Nav->ProjectPointToNavigation(Candidate - FVector(0, 0, HalfHeight), Projected, FVector(50, 50, 300), Data)) return false;
		if (FVector::DistXY(Projected.Location, Candidate) > ArrivalDistance) return false;
		auto* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Heavy->GetNavAgentLocation(), Projected.Location, Heavy.Get());
		if (!Path || !Path->IsValid() || Path->IsPartial()) return false;
	}
	OutPose = Candidate;
	return true;
}

bool ULSShellSiegeController::SelectDeploymentPose()
{
	FVector Chosen = FVector::ZeroVector;
	bool bFound = bHasActiveDeployment && IsDeploymentPoseUsable(ActiveDeploymentLocation,
		FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) > ArrivalDistance, Chosen);
	if (!bFound)
		for (const FVector& Offset : DeploymentOffsets)
		{
			const FVector Candidate = DeploymentLocation + Offset;
			if (IsDeploymentPoseUsable(Candidate, FVector::DistXY(Heavy->GetActorLocation(), Candidate) > ArrivalDistance, Chosen)) { bFound = true; break; }
		}
	if (!bFound)
	{
		if (!bDeploymentBlocked) UE_LOG(LogTemp, Display, TEXT("SHELL_SIEGE_DEPOT_BLOCKED Heavy=%s"), *Heavy->GetName());
		bDeploymentBlocked = true; bHasActiveDeployment = false;
		LastInterruption = TEXT("공성 배치 후보 점유 또는 경로 없음");
		StopControlledMovement(true);
		return false;
	}
	if (!bHasActiveDeployment || !ActiveDeploymentLocation.Equals(Chosen, UE_KINDA_SMALL_NUMBER))
		UE_LOG(LogTemp, Display, TEXT("SHELL_SIEGE_DEPOT Heavy=%s Position=%s"), *Heavy->GetName(), *Chosen.ToCompactString());
	ActiveDeploymentLocation = Chosen;
	bHasActiveDeployment = true; bDeploymentBlocked = false;
	return true;
}

void ULSShellSiegeController::OnAttackExecuted(const FLSAttackExecution& Execution)
{
	if (Phase != ELSShellSiegePhase::Firing || Execution.Source.Get() != Heavy.Get() || !Execution.bProjectile || bShotCommitted) return;
	bShotCommitted = true;
	++ShotsFired;
}

void ULSShellSiegeController::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	if (Phase == ELSShellSiegePhase::Disabled || Phase == ELSShellSiegePhase::Invalid || Phase == ELSShellSiegePhase::Ended || Phase == ELSShellSiegePhase::Dead) return;
	if (!Heavy.IsValid() || !ULSCombatTargetLibrary::IsCombatAlive(Heavy.Get()))
	{
		SetPhase(ELSShellSiegePhase::Dead);
		if (PhaseLabel) PhaseLabel->SetVisibility(false);
		return;
	}
	Heavy->Behavior->SetComponentTickEnabled(false);
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(SiegeTarget.Get())) { EndSiege(); return; }
	const uint64 ExternalFrame = Heavy->Reactive->GetLastExternalForceFrame();
	const FVector Velocity = Heavy->GetVelocity();
	const float StepDeltaSpeed = FVector::Dist(Velocity, PreviousVelocity);
	PreviousVelocity = Velocity;
	const bool bNewForce = ExternalFrame != ObservedExternalForceFrame;
	ObservedExternalForceFrame = ExternalFrame;
	const bool bUpright = FVector::DotProduct(Heavy->GetActorUpVector(), FVector::UpVector) >= FMath::Cos(FMath::DegreesToRadians(MaximumTiltDegrees));
	const bool bGrounded = Heavy->GetCharacterMovement()->IsMovingOnGround() && !Heavy->GetCapsuleComponent()->IsSimulatingPhysics();
	if (bNewForce || !ExternalControlHolds.IsEmpty() || !bUpright || !bGrounded || StepDeltaSpeed >= MechanicalInterruptionDeltaSpeed)
	{
		if (Phase != ELSShellSiegePhase::Recovering)
		{
			LastInterruption = !ExternalControlHolds.IsEmpty() ? TEXT("외부 제어") : TEXT("외력 또는 배치 이탈");
			StopControlledMovement(true);
			SetPhase(ELSShellSiegePhase::Recovering);
		}
		StableSeconds = 0;
	}
	PhaseSeconds += Delta;
	if ((Phase == ELSShellSiegePhase::Loading || Phase == ELSShellSiegePhase::Firing || Phase == ELSShellSiegePhase::Reloading) &&
		(!bHasActiveDeployment || FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) > ArrivalDistance))
	{
		NextMoveAttemptSeconds = 0;
		SetPhase(ELSShellSiegePhase::Moving);
	}
	switch (Phase)
	{
	case ELSShellSiegePhase::Waiting:
		if (GetWorld()->GetTimeSeconds() >= EarliestMoveSeconds) SetPhase(ELSShellSiegePhase::Moving);
		break;
	case ELSShellSiegePhase::Recovering:
		if (!bNewForce && ExternalControlHolds.IsEmpty() && !Heavy->Reactive->IsForcedMotionActive() && bUpright && bGrounded && Velocity.Size() < RecoveryMaximumSpeed) StableSeconds += Delta;
		else StableSeconds = 0;
		if (StableSeconds >= RecoveryStableSeconds)
		{
			StableSeconds = 0; NextMoveAttemptSeconds = 0;
			SetPhase(GetWorld()->GetTimeSeconds() < EarliestMoveSeconds ? ELSShellSiegePhase::Waiting : ELSShellSiegePhase::Moving);
		}
		break;
	case ELSShellSiegePhase::Moving:
		if (GetWorld()->GetTimeSeconds() >= NextMoveAttemptSeconds)
		{
			NextMoveAttemptSeconds = GetWorld()->GetTimeSeconds() + MovementRetrySeconds;
			if (!SelectDeploymentPose()) break;
			auto* Controller = Cast<AAIController>(Heavy->GetController());
			if (!Controller) { Heavy->SpawnDefaultController(); Controller = Cast<AAIController>(Heavy->GetController()); }
			if (Controller && FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) > ArrivalDistance)
				Controller->MoveToLocation(ActiveDeploymentLocation, ArrivalDistance, false, true, true, false, nullptr, false);
		}
		if (bHasActiveDeployment && FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) <= ArrivalDistance)
		{
			StopControlledMovement(false);
			SetPhase(ELSShellSiegePhase::Deploying);
		}
		break;
	case ELSShellSiegePhase::Deploying:
		if (!bHasActiveDeployment || FVector::DistXY(Heavy->GetActorLocation(), ActiveDeploymentLocation) > ArrivalDistance) SetPhase(ELSShellSiegePhase::Moving);
		else if (PhaseSeconds >= DeploySeconds) SetPhase(ELSShellSiegePhase::Loading);
		break;
	case ELSShellSiegePhase::Loading:
		if (PhaseSeconds >= InitialLoadSeconds) SetPhase(ELSShellSiegePhase::Firing);
		break;
	case ELSShellSiegePhase::Reloading:
		if (PhaseSeconds >= ReloadSeconds) SetPhase(ELSShellSiegePhase::Firing);
		break;
	case ELSShellSiegePhase::Firing:
		if (bShotCommitted) SetPhase(ELSShellSiegePhase::Reloading);
		else Heavy->BasicAttack->StartAttack(SiegeTarget.Get());
		break;
	default: break;
	}
	UpdatePresentation();
}

float ULSShellSiegeController::GetPhaseRemainingSeconds() const
{
	if (Phase == ELSShellSiegePhase::Waiting) return GetWorld() ? FMath::Max(0.0, EarliestMoveSeconds - GetWorld()->GetTimeSeconds()) : InitialDelaySeconds;
	const float Duration = Phase == ELSShellSiegePhase::Deploying ? DeploySeconds : Phase == ELSShellSiegePhase::Loading ? InitialLoadSeconds :
		Phase == ELSShellSiegePhase::Reloading ? ReloadSeconds : 0;
	return FMath::Max(0.f, Duration - PhaseSeconds);
}

FString ULSShellSiegeController::GetPhaseText() const
{
	switch (Phase)
	{
	case ELSShellSiegePhase::Waiting: return TEXT("공성 준비");
	case ELSShellSiegePhase::Moving: return bDeploymentBlocked ? TEXT("공성 자리 대기") : TEXT("공성 이동");
	case ELSShellSiegePhase::Deploying: return TEXT("공성 전개");
	case ELSShellSiegePhase::Loading: return TEXT("공성 장전");
	case ELSShellSiegePhase::Firing: return TEXT("공성 발사");
	case ELSShellSiegePhase::Reloading: return TEXT("공성 재장전");
	case ELSShellSiegePhase::Recovering: return TEXT("공성 중단");
	case ELSShellSiegePhase::Dead: return TEXT("공성 무력화");
	case ELSShellSiegePhase::Ended: return TEXT("공성 종료");
	case ELSShellSiegePhase::Invalid: return ConfigurationError;
	default: return FString();
	}
}

FString ULSShellSiegeController::GetWarningText() const
{
	return Phase == ELSShellSiegePhase::Loading || Phase == ELSShellSiegePhase::Reloading
		? FString::Printf(TEXT("%s %.1f초"), *GetPhaseText(), GetPhaseRemainingSeconds()) : GetPhaseText();
}

void ULSShellSiegeController::UpdatePresentation()
{
	if (PhaseLabel) PhaseLabel->SetText(FText::FromString(GetWarningText()));
	if (!Heavy.IsValid() || !SiegeTarget.IsValid() || GetResponseResistanceMultiplier() <= 1) return;
	const FColor Color = Phase == ELSShellSiegePhase::Loading ? FColor(255, 150, 20) : FColor(255, 55, 35);
	DrawDebugLine(GetWorld(), Heavy->GetActorLocation(), SiegeTarget->GetActorLocation(), Color, false, 0, 0, 3);
}

void ULSShellSiegeController::EndSiege()
{
	if (Phase == ELSShellSiegePhase::Dead || Phase == ELSShellSiegePhase::Ended) return;
	SetPhase(ELSShellSiegePhase::Ended);
	StopControlledMovement(true);
	if (PhaseLabel) PhaseLabel->SetVisibility(false);
}

void ULSShellSiegeController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Heavy.IsValid())
	{
		Heavy->BasicAttack->OnAttackExecutedEvent.RemoveAll(this);
		Heavy->BasicAttack->StopAttack();
		Heavy->BasicAttack->SetAttackTargetResolver(FLSResolveAttackTarget());
	}
	Super::EndPlay(Reason);
}
