#include "LSShellExperimentCoordinator.h"
#include "LSShellExperimentGameMode.h"
#include "LSShellProjectile.h"
#include "LSShellEffectInstance.h"
#include "LSShellField.h"
#include "LSShellReactiveComponent.h"
#include "LSShellObstacleResponseComponent.h"
#include "LSShellExperimentEnemy.h"
#include "LSShellWeakPoint.h"
#include "LSShellMaterialStateComponent.h"
#include "LSShellMaterials.h"
#include "LSShellSupplySchedule.h"
#include "LSShellSiegeController.h"
#include "LSShellPhysicalProp.h"
#include "LSShellSplitBudget.h"
#include "LSShellSplitRuntime.h"
#include "LSShellDetonationCue.h"
#include "LSShellLauncherVisual.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Enemies/LSOutpost.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Obstacles/LSPathObstacle.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Maps/LSBattlefieldSettings.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

namespace
{
FLSHealthSnapshot ReadHealth(AActor* Actor)
{
	FLSHealthSnapshot Result;
	if (Actor)
		if (const auto* Health = Actor->FindComponentByClass<ULSHealthComponent>()) Health->TryGetHealth(Result);
	return Result;
}

bool HasBlastLine(UWorld* World, AActor* Target, const FVector& Point, const UPrimitiveComponent* Emitter = nullptr, FHitResult* OutCover = nullptr,
	const TArray<uint64>* IgnoredBodyIds = nullptr, bool bIgnoreMovable = false)
{
	FVector Probe = Target->GetActorLocation();
	if (const auto* Body = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		double Distance;
		LSObstacleGeometry::GetClosestBodyPoint(Body, Point, Probe, Distance);
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellBlast), false, Target);
	if (Emitter) Query.AddIgnoredComponent(Emitter);
	if (IgnoredBodyIds || bIgnoreMovable)
		for (TActorIterator<AActor> It(World); It; ++It)
			if (const auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>())
			{
				bool bWorldAnchored = false;
				if (bIgnoreMovable && Reactive->CanReceiveMomentum())
					for (const auto* Body : ULSShellObstacleResponseComponent::CollectStructuralBodies(Reactive->GetPhysicalBody()))
						if (const auto* Plate = Cast<ALSShellPlate>(Body->GetOwner()); Plate && Plate->IsSupported()) bWorldAnchored = true;
				if ((bIgnoreMovable && Reactive->CanReceiveMomentum() && !bWorldAnchored) ||
					(IgnoredBodyIds && IgnoredBodyIds->Contains(Reactive->BodyId))) Query.AddIgnoredActor(*It);
			}
	Query.bFindInitialOverlaps = true;
	const double Length = FVector::Distance(Probe, Point);
	if (Length < UE_KINDA_SMALL_NUMBER) return true;
	if (bIgnoreMovable)
	{
		for (TActorIterator<ALSShellWeakPoint> It(World); It; ++It) Query.AddIgnoredActor(*It);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
		TArray<FHitResult> Covers;
		World->LineTraceMultiByObjectType(Covers, Probe, Point, Objects, Query);
		for (const auto& Cover : Covers)
			if (const auto* Body = Cover.GetComponent(); Body &&
				(Body->GetCollisionObjectType() == LSCollisionChannels::Building || Body->Mobility != EComponentMobility::Movable)) return false;
		return true;
	}
	FHitResult Hit;
	while (World->LineTraceSingleByChannel(Hit, Probe, Point, LSCollisionChannels::ExplosionOcclusion, Query))
	{
		if (Hit.Distance >= Length - UE_KINDA_SMALL_NUMBER) return true;
		UPrimitiveComponent* Body = Hit.GetComponent();
		if (!Body) return false;
		if (!Cast<APawn>(Hit.GetActor()) && Body->GetCollisionObjectType() != LSCollisionChannels::Unit)
		{
			if (OutCover) *OutCover = Hit;
			return false;
		}
		Query.AddIgnoredComponent(Body);
	}
	return true;
}

double BlastDistance(AActor* Target, FVector Point)
{
	const UPrimitiveComponent* Body = nullptr;
	if (const auto* Obstacle = Cast<ALSPathObstacle>(Target)) Body = Obstacle->GetObstacleBody();
	else if (!Target->IsA<APawn>())
	{
		const auto* Root = Cast<UPrimitiveComponent>(Target->GetRootComponent());
		if (Root && Root->GetCollisionObjectType() == LSCollisionChannels::Building) Body = Root;
	}
	if (Body)
	{
		const double Distance = LSObstacleGeometry::GetPlanarBodyDistance(Body, Point);
		if (FMath::IsFinite(Distance)) return Distance;
	}
	return FVector::Dist2D(Target->GetActorLocation(), Point);
}
}

ALSShellExperimentCoordinator::ALSShellExperimentCoordinator()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	LauncherVisual = CreateDefaultSubobject<ULSShellLauncherVisualComponent>(TEXT("LauncherVisual"));
}

ALSShellExperimentCoordinator* ALSShellExperimentCoordinator::Find(const UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ALSShellExperimentCoordinator> It(World); It; ++It) return *It;
	return nullptr;
}

void ALSShellExperimentCoordinator::BeginPlay()
{
	Super::BeginPlay();
	if (SupplyTuning.Arena.bEnabled)
	{
		const auto& Arena = SupplyTuning.Arena;
		const float Positive[] {Arena.GravityFollowupDelay, Arena.GravityReferenceSpeed, Arena.GravityMinimumSpeed,
			Arena.GravityMaximumSpeed, Arena.GravityMassExponent, Arena.GravityPositionGain, Arena.GravityResponseSeconds,
			Arena.GravityMaximumAcceleration, Arena.GatheredSurfaceDistance, Arena.GatherFollowupHeight,
			Arena.CoolingRadius, Arena.CoolingChildRadius, Arena.CoolingSeconds, Arena.PropelledSettleSpeed, Arena.PropelledSettleSeconds};
		bool bValidArena = IsSupplyExperiment() && SupplyStage == ELSShellSupplyStage::Complete &&
			Arena.GravityMaximumSpeed >= Arena.GravityMinimumSpeed && Arena.GravityFollowupDelay < Tuning.GravitySeconds &&
			FMath::IsFinite(Arena.LandingNormalZ) && Arena.LandingNormalZ > 0 && Arena.LandingNormalZ <= 1;
		for (float Value : Positive) bValidArena &= FMath::IsFinite(Value) && Value > 0;
		if (!bValidArena) { ConfigurationError = TEXT("Arena 효과 설정과 집결·후행 시간을 확인한다."); bStopped = true; }
	}
	if (IsSupplyExperiment())
	{
		bEnableEvents = false;
		First = InitialFirst;
		Second = InitialSecond;
		PendingFirst = First; PendingSecond = Second;
		TArray<ELSShellModule> ExpectedModules {ELSShellModule::Blast, ELSShellModule::Thrust};
		if (SupplyStage >= ELSShellSupplyStage::Charge) ExpectedModules.Add(ELSShellModule::Electric);
		if (SupplyStage >= ELSShellSupplyStage::Materials) ExpectedModules.Append({ELSShellModule::Gravity, ELSShellModule::Iron});
		if (SupplyStage == ELSShellSupplyStage::Complete) ExpectedModules.Append({ELSShellModule::Split, ELSShellModule::Adhesion, ELSShellModule::Cooling});
		bool bValidChoices = AvailableModules.Num() == ExpectedModules.Num();
		for (ELSShellModule Module : ExpectedModules) bValidChoices &= AvailableModules.Contains(Module);
		if (!bValidChoices || InitialFirst == InitialSecond ||
			!CanSelectModule(InitialFirst) || !CanSelectModule(InitialSecond))
		{
			ConfigurationError = TEXT("현재 단계의 선택지와 서로 다른 초기 두 슬롯을 확인한다.");
			bStopped = true;
		}
	}
	const auto* Mode = GetWorld()->GetAuthGameMode<ALSShellExperimentGameMode>();
	if (!Mode || !Mode->IsConfigurationValid())
	{
		ConfigurationError = Mode ? Mode->GetConfigurationError() : TEXT("실험 전용 GameMode가 필요하다.");
		bStopped = true;
	}
	for (TActorIterator<ALSBattlefieldSetup> It(GetWorld()); It; ++It)
	{
		if (!PlayerCastle) PlayerCastle = Cast<ALSCastle>(It->PlayerCastle);
		if (!CoreOutpost) CoreOutpost = It->CoreOutpost;
	}
	if (!PlayerCastle || !CoreOutpost) { ConfigurationError = TEXT("성·중앙 동력로 참조가 필요하다."); bStopped = true; }
	if (PlayerCastle && CoreOutpost)
	{
		LauncherVisual->Initialize(PlayerCastle);
		SetAimPoint(CoreOutpost->GetActorLocation());
	}
	if (auto* State = GetWorld()->GetGameState<ALSGameState>())
		State->OnBattleOutcomeFinalizedEvent.AddDynamic(this, &ThisClass::OnBattleEnded);
	if (bEnableEvents) EventCandidateTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(45.f, 90.f);
	FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::OnWorldPostActorTick);
}

bool ALSShellExperimentCoordinator::IsBattleActive() const
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSShellExperimentGameMode>() : nullptr;
	return Mode && Mode->IsConfigurationValid() && !bStopped && !LSBattleOutcome::AreActionsStopped(GetWorld());
}

float ALSShellExperimentCoordinator::GetConfiguredFollowupDelay() const
{
	FLSShellShot Preview;
	Preview.RuleProfile = RuleProfile;
	Preview.BaseTuning = Tuning;
	Preview.SupplyTuning = SupplyTuning;
	Preview.First = GetModule(0);
	return LSShellFollowupDelay(Preview);
}

float ALSShellExperimentCoordinator::GetMaximumFollowupDelay() const
{
	return IsArenaExperiment() ? FMath::Max(Tuning.ModuleDelay, SupplyTuning.Arena.GravityFollowupDelay) : Tuning.ModuleDelay;
}

bool ALSShellExperimentCoordinator::GetPredictedChargeTarget(FVector Aim, FVector& OutPoint) const
{
	FVector Origin, Muzzle, Direction;
	if (!IsArenaExperiment() || !ComputeShotPose(Aim, Origin, Muzzle, Direction)) return false;
	FHitResult Hit;
	if (!ALSShellProjectile::SweepFlight(GetWorld(), Origin, Aim, PlayerCastle, Hit)) return false;
	OutPoint = Hit.ImpactPoint;
	AActor* Target = ResolveBodyTarget(Hit.GetActor());
	const auto* Reactive = IsValid(Target) ? Target->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
	return Reactive && Reactive->IsPhysicalBodyPresent() && Reactive->bCanStoreCharge;
}

void ALSShellExperimentCoordinator::NotifyChargeFailure(FVector Point, const FString& Reason)
{
	SetTargetFailure(Reason);
	if (IsArenaExperiment()) ShowSupplyCue(ELSShellCueStyle::Failure, Point, FVector::UpVector, 70);
}

void ALSShellExperimentCoordinator::OnWorldPostActorTick(UWorld* World, ELevelTick, float)
{
	if (World != GetWorld()) return;
	// NewlySpawned 틱까지 끝난 뒤 소진한다. 공통 승패 수집은 그 뒤 OnWorldTickEnd에서 실행된다.
	if (!IsBattleActive()) { Reactions.Reset(); return; }
	if (IsSupplyExperiment())
		for (TActorIterator<ALSShellBattery> It(GetWorld()); It; ++It)
			if (It->NeedsInitialCharge() && It->Reactive->IsPhysicalBodyPresent())
			{
				FLSShellShot Origin;
				CaptureShotContext(Origin, *It);
				Origin.bPlayerResource = false;
				if (ApplyCharge(*It, Origin, 360) == 360) It->CommitInitialCharge();
			}
	BurstDetonations = 0;
#if WITH_EDITOR
	if (bRequestCoreShotObservation)
	{
		bRequestCoreShotObservation = false;
		bObservationShotAccepted = CoreOutpost && FireAt(CoreOutpost->GetActorLocation());
	}
	if (bRequestContactObservation)
	{
		bRequestContactObservation = false;
		bObservationShotAccepted = false;
		if (GetWorld()->WorldType == EWorldType::PIE && ObservationTarget && ObservationTarget->GetWorld() == GetWorld()
			&& ULSCombatTargetLibrary::IsCombatAlive(ObservationTarget) && !bComposing && GetWorld()->GetTimeSeconds() >= NextFireTime)
		{
			ObservationTarget->GetCharacterMovement()->StopMovementImmediately();
			ObservationTarget->SetActorLocation(ObservationTarget->Behavior->GetInitialPosition(), false, nullptr, ETeleportType::TeleportPhysics);
			bObservationShotAccepted = FireAt(ObservationTarget->GetActorLocation());
		}
	}
#endif
	// 알림 중 추가한 반응도 같은 프레임에 소진한다. 배열 참조를 콜백 너머로 보존하지 않는다.
	for (int32 Index = 0; Index < Reactions.Num() && IsBattleActive(); ++Index)
	{
		const FQueuedReaction Event = Reactions[Index];
		switch (Event.Kind)
		{
		case FQueuedReaction::EKind::Explosion: ResolveExplosion(Event.Explosion); break;
		case FQueuedReaction::EKind::ShellHit: ResolveShellHit(Event.Shot, Event.Hit, Event.bCombatImpact); break;
		case FQueuedReaction::EKind::Contact: ResolveContact(Event); break;
		case FQueuedReaction::EKind::PointThrust: ResolvePointThrust(Event.Hit.ImpactPoint, Event.Shot); break;
		case FQueuedReaction::EKind::Cooling: ResolveCooling(Event.Hit.ImpactPoint, Event.Shot, Event.Hit); break;
		case FQueuedReaction::EKind::GatherFollowup: ResolveGatherFollowup(Event.GatherModule, Event.Shot, Event.GatherOutput); break;
		}
	}
	if (BurstDetonations > 0)
		SetReactionText(FString::Printf(TEXT("동시 기폭 %d회 · 연결선이 원인을 표시한다"), BurstDetonations));
	Reactions.Reset();
	UpdateEvent();
}

bool ALSShellExperimentCoordinator::ComputeShotPose(FVector Aim, FVector& Origin, FVector& Muzzle, FVector& Direction) const
{
	if (!PlayerCastle || Aim.ContainsNaN()) return false;
	Origin = PlayerCastle->Weapons->GetWeaponFireOrigin(ELSCastleWeapon::Cannon, Aim);
	Direction = (Aim - Origin).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	const UCapsuleComponent* Capsule = PlayerCastle->GetCapsuleComponent();
	const FVector Axis = Capsule->GetUpVector() * (Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius());
	const FVector Center = Capsule->GetComponentLocation();
	const double ClearanceRadius = Capsule->GetScaledCapsuleRadius() + 8;
	auto IsInside = [&](double Distance)
	{
		const FVector Point = Origin + Direction * Distance;
		return FVector::DistSquared(Point, FMath::ClosestPointOnSegment(Point, Center - Axis, Center + Axis)) < FMath::Square(ClearanceRadius);
	};
	double Low = 0, High = 2 * (Capsule->GetScaledCapsuleHalfHeight() + FVector::Distance(Origin, Center) + 8);
	if (IsInside(0))
	{
		for (int32 Step = 0; Step < 24; ++Step)
		{
			const double Middle = (Low + High) * 0.5;
			if (IsInside(Middle)) Low = Middle; else High = Middle;
		}
	}
	else High = 0;
	Muzzle = Origin + Direction * (High + Tuning.MuzzleExtension);
	return true;
}

void ALSShellExperimentCoordinator::SetAimPoint(FVector Aim)
{
	FVector Origin, Muzzle, Direction;
	if (LauncherVisual && ComputeShotPose(Aim, Origin, Muzzle, Direction))
	{
		FHitResult Hit;
		if (ALSShellProjectile::SweepFlight(GetWorld(), Origin, Muzzle, PlayerCastle, Hit)) Muzzle = Hit.ImpactPoint;
		LauncherVisual->SetPose(Origin, Muzzle, Direction);
	}
}

bool ALSShellExperimentCoordinator::FireAt(FVector Aim)
{
	if (!IsBattleActive() || bComposing || GetWorld()->GetTimeSeconds() < NextFireTime) return false;
	FVector Origin, Start, Direction;
	if (!ComputeShotPose(Aim, Origin, Start, Direction)) return false;
	FLSShellShot Shot;
	CaptureShotContext(Shot);
	Shot.Source = PlayerCastle; Shot.Faction = PlayerCastle->GetFaction();
	Shot.Origin = Origin; Shot.Direction = Direction;
	Shot.First = First; Shot.Second = Second; Shot.Damage = Tuning.DirectDamage;
	FHitResult Hit;
	FVector VisibleMuzzle = Start;
	// 실제 포신까지의 구간도 검사해 엄폐나 가까운 대상을 건너뛰지 않는다.
	if (ALSShellProjectile::SweepFlight(GetWorld(), Origin, Start, PlayerCastle, Hit))
	{
		QueueShellHit(Shot, Hit);
		VisibleMuzzle = Hit.ImpactPoint;
	}
	else
	{
		auto* Projectile = GetWorld()->SpawnActor<ALSShellProjectile>(Origin, Direction.Rotation());
		Projectile->Launch(this, Shot, Start, Direction);
	}
	NextFireTime = GetWorld()->GetTimeSeconds() + Tuning.ReloadSeconds;
	LauncherVisual->SetPose(Origin, VisibleMuzzle, Direction);
	LauncherVisual->Fire(IsSupplyExperiment() ? SupplyRecoilScale : 1);
	ShowPulse(VisibleMuzzle, 125, FLinearColor(3, 1.6f, 0.25f));
	if (FireSound) UGameplayStatics::PlaySoundAtLocation(this, FireSound, VisibleMuzzle);
	SetReactionText(TEXT("포탄 발사"));
	return true;
}

void ALSShellExperimentCoordinator::QueueExplosion(const FLSShellExplosion& Explosion)
{
	if (!IsBattleActive()) return;
	FQueuedReaction Event; Event.Kind = FQueuedReaction::EKind::Explosion; Event.Explosion = Explosion;
	Reactions.Add(Event);
}

void ALSShellExperimentCoordinator::QueuePointThrust(FVector Point, const FLSShellShot& Shot)
{
	if (!IsBattleActive() || Shot.RuleProfile != ELSShellRuleProfile::SupplyStageOne) return;
	FQueuedReaction Event;
	Event.Kind = FQueuedReaction::EKind::PointThrust;
	Event.Hit.ImpactPoint = Point;
	Event.Shot = Shot;
	Reactions.Add(Event);
}

void ALSShellExperimentCoordinator::QueueGatherFollowup(ELSShellModule Module, const FLSShellShot& Shot, const FLSShellGatherOutput& Output)
{
	if (!IsBattleActive() || !LSShellUsesArena(Shot)) return;
	FQueuedReaction Event;
	Event.Kind = FQueuedReaction::EKind::GatherFollowup;
	Event.GatherModule = Module;
	Event.Shot = Shot;
	Event.GatherOutput = Output;
	TMap<uint64, int32> Indices;
	for (int32 Index = 0; Index < Event.GatherOutput.Members.Num(); ++Index)
		Indices.Add(Event.GatherOutput.Members[Index].BodyId, Index);
	TSet<uint64> Visited;
	for (FLSShellGatherMember& Member : Event.GatherOutput.Members)
	{
		if (Visited.Contains(Member.BodyId)) continue;
		TArray<uint64> Group {Member.BodyId};
		Visited.Add(Member.BodyId);
		uint64 GroupId = Member.BodyId;
		for (int32 Cursor = 0; Cursor < Group.Num(); ++Cursor)
		{
			GroupId = FMath::Min(GroupId, Group[Cursor]);
			const auto* Reactive = Event.GatherOutput.Members[Indices[Group[Cursor]]].Reactive.Get();
			if (!Reactive) continue;
			for (UPrimitiveComponent* Body : Reactive->GetStructuralContactBodies())
				if (const auto* Other = IsValid(Body) && IsValid(Body->GetOwner()) ? Body->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>() : nullptr)
					if (Indices.Contains(Other->BodyId) && !Visited.Contains(Other->BodyId))
					{ Visited.Add(Other->BodyId); Group.Add(Other->BodyId); }
		}
		for (uint64 Id : Group) Event.GatherOutput.Members[Indices[Id]].AssemblyId = GroupId;
	}
	Reactions.Add(MoveTemp(Event));
}

void ALSShellExperimentCoordinator::QueueShellHit(const FLSShellShot& Shot, const FHitResult& Hit)
{
	if (!IsBattleActive()) return;
	FQueuedReaction Event; Event.Kind = FQueuedReaction::EKind::ShellHit; Event.Shot = Shot; Event.Hit = Hit;
	Event.bCombatImpact = ULSCombatTargetLibrary::IsCombatAlive(Hit.GetActor());
	Reactions.Add(Event);
}

void ALSShellExperimentCoordinator::QueueContact(AActor* Moving, AActor* Other, FVector Point, FVector Normal, FVector Velocity, FVector OtherVelocity, uint64 ContactEpoch)
{
	if (!IsBattleActive()) return;
	if (ContactEpoch != 0)
	{
		if (SeenContactEpochs.Contains(ContactEpoch)) return;
		SeenContactEpochs.Add(ContactEpoch);
	}
	FQueuedReaction Event; Event.Kind = FQueuedReaction::EKind::Contact;
	Event.Moving = Moving; Event.Other = Other; Event.Hit.ImpactPoint = Point; Event.Hit.ImpactNormal = Normal;
	Event.Velocity = Velocity; Event.OtherVelocity = OtherVelocity;
	if (const auto* Reactive = Moving->FindComponentByClass<ULSShellReactiveComponent>())
	{ Event.MovingBodyId = Reactive->BodyId; Event.MovingGeneration = Reactive->GetBodyGeneration(); Event.bThrustPropelled |= Reactive->IsThrustPropelled(); }
	if (const auto* Reactive = Other->FindComponentByClass<ULSShellReactiveComponent>())
	{ Event.OtherBodyId = Reactive->BodyId; Event.OtherGeneration = Reactive->GetBodyGeneration(); Event.bThrustPropelled |= Reactive->IsThrustPropelled(); }
	Event.StepSeconds = GetWorld()->GetDeltaSeconds();
	Event.GameTime = GetWorld()->GetTimeSeconds();
	Event.ContactEpoch = ContactEpoch;
	Event.ChargeSequence = ++ChargeContactSequence;
	Event.DischargeBudget = SupplyTuning.DischargePerContact;
	Event.DischargeMinimumSpeed = SupplyTuning.DischargeMinimumSpeed;
	Reactions.Add(Event);
}

void ALSShellExperimentCoordinator::ApplyExperimentDamage(AActor* Receiver, float Amount, const FLSShellShot& Shot, FVector Point, FVector Direction, ELSShellDamageCause Cause)
{
	if (!IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(Receiver) || Amount <= 0) return;
	if (IsSupplyExperiment() && Cause == ELSShellDamageCause::Impact)
		if (const auto* Material = Receiver->FindComponentByClass<ULSShellMaterialStateComponent>()) Amount *= Material->GetKineticDamageMultiplier();
	if (Receiver == CoreOutpost && (Cause == ELSShellDamageCause::Shell || Cause == ELSShellDamageCause::Module ||
		(Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne &&
			(Cause == ELSShellDamageCause::Electric || Cause == ELSShellDamageCause::Impact) && Shot.bPlayerResource))) Amount *= 0.5f;
	FLSDamageRequest Request;
	Request.DamageAmount = Amount;
	Request.SourceActor = Shot.Source.Get(); Request.DamageCauser = this;
	Request.bHasSourceFaction = true; Request.SourceFaction = Shot.Faction;
	Request.bHasAttackOrigin = true; Request.AttackOrigin = Shot.Origin;
	Request.bHasImpactLocation = true; Request.ImpactLocation = Point;
	Request.bHasIncomingDirection = !Direction.IsNearlyZero(); Request.IncomingDirection = Direction.GetSafeNormal();
	auto* Reactive = Receiver->FindComponentByClass<ULSShellReactiveComponent>();
	if (IsSupplyExperiment())
		if (auto* Material = Receiver->FindComponentByClass<ULSShellMaterialStateComponent>(); Material && (Material->IsCooled() || LSShellUsesArena(Shot)))
			Material->CaptureBeforePhysicalDeath();
	ELSShellDetonationTrigger Trigger = ELSShellDetonationTrigger::Damage;
	if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{
		switch (Cause)
		{
		case ELSShellDamageCause::Shell: Trigger = ELSShellDetonationTrigger::Shell; break;
		case ELSShellDamageCause::Module: Trigger = ELSShellDetonationTrigger::ModuleBlast; break;
		case ELSShellDamageCause::Impact: Trigger = ELSShellDetonationTrigger::Impact; break;
		case ELSShellDamageCause::Detonation: Trigger = ELSShellDetonationTrigger::Chain; break;
		case ELSShellDamageCause::Electric: Trigger = ELSShellDetonationTrigger::Discharge; break;
		}
	}
	// 공통 사망 델리게이트가 동기 호출되는 동안만 원인을 제공한다. 다음 외부 피해에는 남기지 않는다.
	ELSShellDetonationTrigger UnusedTrigger = ELSShellDetonationTrigger::Damage;
	TGuardValue<ELSShellDetonationTrigger> TriggerScope(Reactive ? Reactive->DamageFeedbackTrigger : UnusedTrigger, Trigger);
	const FString ReceiverName = Receiver->GetName();
	const FString SourceName = GetNameSafe(Shot.Source.Get());
	const double DamageTime = GetWorld()->GetTimeSeconds();
	const FLSApplyDamageResult Result = Receiver->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(Request);
	if (IsSupplyExperiment() && Result.AppliedDamage > 0)
		UE_LOG(LogTemp, Display, TEXT("SHELL_DAMAGE Time=%.6f Cause=%d Shot=%llu SourceId=%llu Source=%s Receiver=%s Applied=%.3f PlayerResource=%d Origin=%s Point=%s"),
			DamageTime, static_cast<int32>(Cause), Shot.ShotId, Shot.SourceId, *SourceName, *ReceiverName,
			Result.AppliedDamage, Shot.bPlayerResource, *Shot.Origin.ToCompactString(), *Point.ToCompactString());
	if (Receiver == CoreOutpost) CoreDamageCommitted += Result.AppliedDamage;
}

void ALSShellExperimentCoordinator::ResolveShellHit(const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact)
{
	AActor* BodyTarget = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne ? ResolveBodyTarget(Hit.GetActor()) : nullptr;
	ULSShellReactiveComponent* CapturedBody = BodyTarget ? BodyTarget->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
	++ShellHitCount;
	LastShellImpact = Hit.ImpactPoint;
	const bool bArenaCooling = LSShellUsesArena(Shot) && Shot.First == ELSShellModule::Cooling;
	if ((Shot.RuleProfile != ELSShellRuleProfile::SupplyStageOne || Shot.First != ELSShellModule::Thrust) && !bArenaCooling)
	{
		float Damage = Shot.Damage;
		if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && !Shot.bChild)
		{
			if (Shot.First == ELSShellModule::Split) Damage = 0;
			else if (Shot.Second == ELSShellModule::Split) Damage *= .5f;
		}
		ApplyExperimentDamage(Hit.GetActor(), Damage, Shot, Hit.ImpactPoint, Shot.Direction, ELSShellDamageCause::Shell);
	}
	if (auto* Current = ResolveCurrentBody(CapturedBody)) BodyTarget = Current->GetOwner();
	if (AActor* Target = BodyTarget ? ResolveBodyTarget(BodyTarget) : Hit.GetActor(); IsValid(Target))
		if (auto* Reactive = Target->FindComponentByClass<ULSShellReactiveComponent>())
		{
			if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
			{
				const float SplitScale = !Shot.bChild && Shot.First == ELSShellModule::Split ? 0 :
					(!Shot.bChild && Shot.Second == ELSShellModule::Split ? .5f : 1.f);
				if (!bArenaCooling) Reactive->ApplyMomentum(Shot.Direction.GetSafeNormal() * Shot.SupplyTuning.DirectMomentum * SplitScale);
			}
			else if (ULSCombatTargetLibrary::IsCombatAlive(Target))
				Reactive->ApplyImpulseVelocity(Shot.Direction.GetSafeNormal2D() * Tuning.DirectVelocity + FVector(0, 0, Tuning.DirectLift));
		}
	const auto* HitReactive = IsValid(Hit.GetActor()) ? Hit.GetActor()->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
	const bool bThrustSuppliesHitSound = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne &&
		Shot.First == ELSShellModule::Thrust && HitReactive && HitReactive->CanReceiveMomentum() &&
		!Shot.Direction.GetSafeNormal2D().IsNearlyZero();
	const bool bModuleSuppliesHitCue = bThrustSuppliesHitSound ||
		(Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.First == ELSShellModule::Blast);
	if (!bModuleSuppliesHitCue)
	{
		ShowPulse(Hit.ImpactPoint, 160, FLinearColor(3, 2.2f, .7f));
		if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Hit.ImpactPoint);
	}
	if (Shot.bChild && !Shot.bInheritedModule) return;
	auto* Effect = GetWorld()->SpawnActor<ALSShellEffectInstance>(Hit.ImpactPoint, FRotator::ZeroRotator);
	Effect->Initialize(this, Shot, Hit, bCombatImpact, ResolveBodyTarget(BodyTarget));
}

AActor* ALSShellExperimentCoordinator::ResolveBodyTarget(AActor* Target) const
{
	if (!IsValid(Target)) return nullptr;
	if (const auto* WeakPoint = Cast<ALSShellWeakPoint>(Target)) Target = WeakPoint->GetHeavyParent();
	if (!IsValid(Target)) return nullptr;
	if (auto* Reactive = Target->FindComponentByClass<ULSShellReactiveComponent>())
		if (auto* Current = ResolveCurrentBody(Reactive)) return Current->GetOwner();
	return Target;
}

ULSShellReactiveComponent* ALSShellExperimentCoordinator::ResolveCurrentBody(ULSShellReactiveComponent* Previous) const
{
	TSet<ULSShellReactiveComponent*> Visited;
	while (Previous && !Visited.Contains(Previous))
	{
		Visited.Add(Previous);
		const auto* Replacement = BodyReplacements.Find(Previous);
		if (!Replacement) return Previous;
		Previous = Replacement->Get();
	}
	return Previous;
}

void ALSShellExperimentCoordinator::RemapPhysicalBody(ULSShellReactiveComponent* Previous, ULSShellReactiveComponent* Replacement)
{
	if (!Previous || !Replacement || Previous == Replacement || !Replacement->IsPhysicalBodyPresent() || !Previous->BodyId) return;
	FLSShellShot PreviousSource;
	CaptureShotContext(PreviousSource, Previous->GetOwner());
	RemappedSourceIdentities.Add(Replacement->GetOwner(), {PreviousSource.SourceId, PreviousSource.Faction});
	UPrimitiveComponent* OldBody = Previous->GetPhysicalBody();
	UPrimitiveComponent* NewBody = Replacement->GetPhysicalBody();
	Replacement->ClearContactRelations();
	Replacement->ChargeStore = MoveTemp(Previous->ChargeStore);
	Replacement->BodyId = Previous->BodyId;
	Replacement->BodyGeneration = Previous->BodyGeneration + 1;
	Replacement->LastExternalForceFrame = Previous->LastExternalForceFrame;
	for (const auto& Contact : Previous->Contacts)
	{
		UPrimitiveComponent* Other = Contact.Key.Get();
		if (!IsValid(Other)) continue;
		auto* Peer = Other->GetOwner()->FindComponentByClass<ULSShellReactiveComponent>();
		if (Peer) { Peer->Contacts.Remove(OldBody); Peer->StructuralContacts.Remove(OldBody); }
		if (!Other->IsQueryCollisionEnabled() || !Other->OverlapComponent(NewBody->GetComponentLocation(),
			NewBody->GetComponentQuat(), NewBody->GetCollisionShape(3))) continue;
		Replacement->Contacts.Add(Other, Contact.Value);
		if (Peer) Peer->Contacts.Add(NewBody, Contact.Value);
	}
	Previous->Contacts.Reset(); Previous->StructuralContacts.Reset(); Previous->BodyId = 0;
	BodyReplacements.Add(Previous, Replacement);
	RemappedBodyIdentities.Add(Replacement->BodyId, Replacement);
	GeneratedMaterials.Remap(Previous->GetOwner(), Replacement->GetOwner());
	for (TActorIterator<ALSShellEffectInstance> It(GetWorld()); It; ++It) It->RemapAnchor(Previous->GetOwner(), Replacement->GetOwner());
	for (TActorIterator<ALSShellSupplySchedule> It(GetWorld()); It; ++It) It->NotifyDeliveryActorReplaced(Previous->GetOwner(), Replacement->GetOwner());
}

void ALSShellExperimentCoordinator::HandleMaterialDeath(AActor* Target)
{
	if (!IsSupplyExperiment() || !IsBattleActive() || !IsValid(Target)) return;
	auto* Material = Target->FindComponentByClass<ULSShellMaterialStateComponent>();
	auto* Reactive = Target->FindComponentByClass<ULSShellReactiveComponent>();
	if (!Reactive || !Reactive->BodyId) return;
	const FLSShellRigidBodyState State = Material ? Material->CaptureBodyState() : FLSShellRigidBodyState();
	if (Material && Material->IsCooled())
	{
		if (const auto* Prop = Cast<ALSShellPhysicalProp>(Target); Prop && Prop->DoesRetainPhysicalBodyOnDeath())
		{ Material->SetPermanentIce(); return; }
		const auto* Enemy = Cast<ALSShellExperimentEnemy>(Target);
		if (Enemy && Enemy->EnemyKind != ELSShellEnemyKind::Carrier)
		{
			const auto Transfer = Material->CreateIceReplacement(State);
			if (Transfer.IsValid())
			{
				RemapPhysicalBody(Reactive, Transfer.ReplacementReactive.Get());
				Material->CommitMaterialTransfer(Transfer.Ice->MaterialState);
				return;
			}
		}
	}
	if (const auto* Enemy = Cast<ALSShellExperimentEnemy>(Target); Enemy && Enemy->IsHeavy() && State.bValid)
	{
		auto* Carriage = GetWorld()->SpawnActorDeferred<ALSShellSiegeCarriage>(ALSShellSiegeCarriage::StaticClass(), State.Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Carriage && Carriage->InitializeBody(State))
		{
			Carriage->FinishSpawning(State.Transform);
			RemapPhysicalBody(Reactive, Carriage->Reactive);
			if (Material) Material->CommitMaterialTransfer(Carriage->MaterialState);
		}
		else if (Carriage) Carriage->Destroy();
	}
}

bool ALSShellExperimentCoordinator::RecallGeneratedMaterial(AActor* Target)
{
	if (!IsSupplyExperiment() || !IsBattleActive() || !GeneratedMaterials.IsGenerated(Target)) return false;
	for (TActorIterator<ALSShellEffectInstance> It(GetWorld()); It; ++It) It->CancelForBodyRemoval(Target);
	if (auto* Material = Target->FindComponentByClass<ULSShellMaterialStateComponent>()) Material->ReleaseAllBonds();
	return GeneratedMaterials.Recall(Target);
}

void ALSShellExperimentCoordinator::ResolveExplosion(const FLSShellExplosion& Explosion)
{
	if (Explosion.Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{
		ResolveSupplyExplosion(Explosion);
		return;
	}
	FLSShellShot DamageShot = Explosion.Shot;
	DamageShot.Origin = Explosion.Location;
	if (Explosion.Cause == ELSShellDamageCause::Detonation) { ++DetonationCount; ++BurstDetonations; }
	struct FTarget { TWeakObjectPtr<AActor> Actor; float Scale; FVector Direction, Point; };
	TArray<FTarget> Targets;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!ULSCombatTargetLibrary::IsCombatAlive(*It)) continue;
		const double Distance = BlastDistance(*It, Explosion.Location);
		if (Distance >= Explosion.Radius || !HasBlastLine(GetWorld(), *It, Explosion.Location)) continue;
		FVector Direction = (It->GetActorLocation() - Explosion.Location).GetSafeNormal2D();
		if (Direction.IsNearlyZero()) Direction = Explosion.Shot.Direction.GetSafeNormal2D();
		Targets.Add({*It, static_cast<float>(1 - Distance / Explosion.Radius), Direction, It->GetActorLocation()});
	}
	if (Explosion.Cause == ELSShellDamageCause::Detonation)
	{
		if (AActor* Emitter = Explosion.Shot.Source.Get(); ULSCombatTargetLibrary::IsCombatAlive(Emitter))
		{
			const auto* Heavy = Cast<ALSShellExperimentEnemy>(Emitter);
			const auto* Reactive = Emitter->FindComponentByClass<ULSShellReactiveComponent>();
			if ((Reactive && Reactive->IsDetonationQueued()) ||
				(Explosion.Trigger == ELSShellDetonationTrigger::WeakPoint && Heavy && Heavy->HasCommittedWeakPointDetonation()))
				ApplyExperimentDamage(Emitter, ReadHealth(Emitter).CurrentHealth, DamageShot, Explosion.Location, FVector::UpVector, Explosion.Cause);
		}
	}
	// 같은 폭발의 노출·감쇠 값은 위에서 고정한다. 약점의 사망 예약이 본체 정리보다 먼저 발생해야 한다.
	Targets.StableSort([](const FTarget& A, const FTarget& B)
	{
		return A.Actor.IsValid() && A.Actor->IsA<ALSShellWeakPoint>() && !(B.Actor.IsValid() && B.Actor->IsA<ALSShellWeakPoint>());
	});
	for (const FTarget& Target : Targets)
	{
		AActor* Actor = Target.Actor.Get();
		ApplyExperimentDamage(Actor, Explosion.Damage * Target.Scale, DamageShot, Target.Point, Target.Direction, Explosion.Cause);
		if (!ULSCombatTargetLibrary::IsCombatAlive(Actor)) continue;
		if (auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>(); Reactive && Explosion.Velocity > 0)
		{
			float OutwardSpeed = Explosion.Velocity * Target.Scale;
			float LiftSpeed = OutwardSpeed * 0.22f;
			if (Explosion.Cause == ELSShellDamageCause::Module)
			{
				OutwardSpeed = FMath::Max(OutwardSpeed, Tuning.BlastMinimumVelocity * Explosion.Shot.EffectScale);
				LiftSpeed = FMath::Max(LiftSpeed, Tuning.BlastMinimumLift * Explosion.Shot.EffectScale);
			}
			Reactive->ApplyImpulseVelocity(Target.Direction * OutwardSpeed + FVector::UpVector * LiftSpeed);
		}
	}
	if (Explosion.Cause == ELSShellDamageCause::Detonation)
	{
		FLSShellCueSpec Cue;
		Cue.Location = Explosion.Location; Cue.Origin = Explosion.TriggerOrigin;
		Cue.bDrawLink = Explosion.bHasTriggerOrigin;
		Cue.Radius = Explosion.Radius; Cue.Strength = Explosion.Damage >= 500 ? 1.6f : 1;
		Cue.DelaySeconds = FMath::Max(0, BurstDetonations - 1) * 0.07f;
		Cue.Caption = Explosion.Trigger == ELSShellDetonationTrigger::Impact ? TEXT("IMPACT") :
			(Explosion.Trigger == ELSShellDetonationTrigger::WeakPoint ? TEXT("WEAK BLAST") :
				(Explosion.Trigger == ELSShellDetonationTrigger::Proximity ? TEXT("SELF BLAST") : TEXT("DETONATE")));
		Cue.Sound = ExplosionSound;
		auto* Visual = GetWorld()->SpawnActor<ALSShellDetonationCue>(Explosion.Location, FRotator::ZeroRotator);
		Visual->Initialize(Cue);
	}
	else
	{
		ShowPulse(Explosion.Location, Explosion.Radius, FLinearColor(3, 0.8f, 0.12f), Explosion.Damage >= 500);
		if (ExplosionSound) UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, Explosion.Location);
	}
}

void ALSShellExperimentCoordinator::ResolveSupplyExplosion(const FLSShellExplosion& Explosion)
{
	struct FTarget
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<ULSShellReactiveComponent> Reactive;
		uint64 BodyId = 0;
		FVector Point, Direction, Impulse = FVector::ZeroVector;
		float Scale;
	};
	FLSShellShot DamageShot = Explosion.Shot;
	DamageShot.Origin = Explosion.Location;
	TArray<FTarget> Targets;
	float SumWeights = 0;
	TSet<UPrimitiveComponent*> ShownBlockers;
	if (GetWorld()->GetTimeSeconds() > ShieldUntil) ShieldPoints.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Explosion.EmitterBody.IsValid() && Explosion.EmitterBody->GetOwner() == Actor) continue;
		auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>();
		if (!ULSCombatTargetLibrary::IsCombatAlive(Actor) && !(Reactive && Reactive->IsPhysicalBodyPresent())) continue;
		const double Distance = BlastDistance(Actor, Explosion.Location);
		if (Distance >= Explosion.Radius) continue;
		FHitResult Cover;
		if (!HasBlastLine(GetWorld(), Actor, Explosion.Location, Explosion.EmitterBody.Get(), &Cover,
			Explosion.IgnoredCoverBodyIds.IsEmpty() ? nullptr : &Explosion.IgnoredCoverBodyIds))
		{
			if (UPrimitiveComponent* Blocker = Cover.GetComponent(); Blocker && !ShownBlockers.Contains(Blocker))
			{
				ShownBlockers.Add(Blocker);
				ShieldPoints.Add(Cover.ImpactPoint);
				ShieldUntil = GetWorld()->GetTimeSeconds() + .15f;
			}
			UE_LOG(LogTemp, Display, TEXT("SHELL_SUPPLY_SHIELD Target=%s Blocker=%s Point=%s"), *Actor->GetName(),
				*GetNameSafe(Cover.GetActor()), *Cover.ImpactPoint.ToCompactString());
			continue;
		}
		FVector Point = Actor->GetActorLocation();
		UPrimitiveComponent* Body = Reactive ? Reactive->GetPhysicalBody() : Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (Body)
		{
			double SurfaceDistance;
			LSObstacleGeometry::GetClosestBodyPoint(Body, Explosion.Location, Point, SurfaceDistance);
		}
		FVector Planar = (Actor->GetActorLocation() - Explosion.Location).GetSafeNormal2D();
		if (Planar.IsNearlyZero()) Planar = Explosion.Shot.Direction.GetSafeNormal2D();
		FVector Direction = (Planar + FVector::UpVector * Explosion.Shot.SupplyTuning.BlastLiftRatio).GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = FVector::UpVector;
		const float Scale = 1 - Distance / Explosion.Radius;
		Targets.Add({Actor, Reactive, Reactive ? Reactive->BodyId : 0, Point, Direction, FVector::ZeroVector, Scale});
		if (Reactive && Reactive->CanReceiveMomentum()) SumWeights += Scale;
	}
	const float Budget = Explosion.Cause == ELSShellDamageCause::Module
		? Explosion.Shot.SupplyTuning.BlastMomentum * Explosion.Shot.EffectScale
		: Explosion.Velocity * Explosion.Shot.SupplyTuning.ReferenceMassKg;
	// 파괴로 몸체나 화물 상태가 바뀌어도 이 폭발의 노출 대상과 충격 총량을 다시 선정하지 않는다.
	for (FTarget& Target : Targets)
		if (Target.Reactive.IsValid() && Target.Reactive->CanReceiveMomentum())
		{
			Target.Impulse = (Explosion.Cause == ELSShellDamageCause::Module ? FVector::UpVector : Target.Direction)
				* Budget * Target.Scale / (LSShellUsesArena(Explosion.Shot) ? 1.f : FMath::Max(1.f, SumWeights));
			if (Explosion.Cause == ELSShellDamageCause::Module)
				Target.Impulse = Target.Impulse.GetClampedToMaxSize(FMath::Max(0.f,
					Explosion.Shot.SupplyTuning.ModuleMaximumDeltaSpeed) * Target.Reactive->GetPhysicsMassKg());
		}
	if (Explosion.Cause == ELSShellDamageCause::Detonation)
	{
		++DetonationCount;
		if (AActor* Emitter = Explosion.Shot.Source.Get(); ULSCombatTargetLibrary::IsCombatAlive(Emitter))
			ApplyExperimentDamage(Emitter, ReadHealth(Emitter).CurrentHealth, DamageShot, Explosion.Location, FVector::UpVector, Explosion.Cause);
	}
	Targets.StableSort([](const FTarget& A, const FTarget& B)
	{
		return A.Actor.IsValid() && A.Actor->IsA<ALSShellWeakPoint>() && !(B.Actor.IsValid() && B.Actor->IsA<ALSShellWeakPoint>());
	});
	for (const FTarget& Target : Targets)
	{
		AActor* Actor = Target.Actor.Get();
		ULSShellReactiveComponent* PreviousBody = Target.Reactive.Get();
		ApplyExperimentDamage(Actor, Explosion.Damage * Target.Scale, DamageShot, Target.Point, Target.Direction, Explosion.Cause);
		auto* Current = RemappedBodyIdentities.FindRef(Target.BodyId).Get();
		if (!Current) Current = ResolveCurrentBody(PreviousBody);
		if (Current && Current->ApplyMomentum(Target.Impulse)) Current->NotifyExternalForce();
	}
	if (Explosion.Cause == ELSShellDamageCause::Detonation)
	{
		ShowSupplyCue(ELSShellCueStyle::Chain, Explosion.Location, Explosion.Shot.Direction,
			Explosion.Radius, 1, Explosion.Trigger, Explosion.TriggerOrigin, Explosion.bHasTriggerOrigin);
	}
	else
	{
		ShowSupplyCue(ELSShellCueStyle::Blast, Explosion.Location, Explosion.Shot.Direction, Explosion.Radius);
	}
}

float ALSShellExperimentCoordinator::ApplyCharge(AActor* Target, const FLSShellShot& Shot, float Amount)
{
	if (!IsBattleActive() || Shot.RuleProfile != ELSShellRuleProfile::SupplyStageOne) return 0;
	auto* Reactive = IsValid(Target) ? Target->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
	if (!Reactive || !Reactive->IsPhysicalBodyPresent())
	{ if (LSShellUsesArena(Shot)) NotifyChargeFailure(LastShellImpact, TEXT("충전 대상 없음")); else NotifyMissingTarget(); return 0; }
	if (!Reactive->bCanStoreCharge)
	{ if (LSShellUsesArena(Shot)) NotifyChargeFailure(LastShellImpact, TEXT("충전 불가")); else SetTargetFailure(TEXT("전하를 저장할 수 없음")); return 0; }
	FLSShellChargePacket Packet;
	Packet.PacketId = ++NextChargePacketId;
	Packet.Sequence = Packet.PacketId;
	Packet.SourceId = Shot.SourceId;
	Packet.ShotId = Shot.ShotId;
	Packet.Source = Shot.Source;
	Packet.Faction = Shot.Faction;
	Packet.Provenance = Shot.bPlayerResource ? ELSShellChargeProvenance::Player : ELSShellChargeProvenance::Environment;
	Packet.Amount = Amount;
	Packet.ActivationSequence = ++ChargeContactSequence;
	const float Accepted = Reactive->AddCharge(Packet, Shot.SupplyTuning.ChargeCapacity);
	if (LSShellUsesArena(Shot) && Accepted <= UE_KINDA_SMALL_NUMBER)
	{ NotifyChargeFailure(Target->GetActorLocation(), TEXT("전하 소산 · 저장 한도")); return 0; }
	FVector ChargePoint = Target->GetActorLocation();
	if (LSShellUsesArena(Shot))
	{
		double Distance;
		LSObstacleGeometry::GetClosestBodyPoint(Reactive->GetPhysicalBody(), Shot.Origin, ChargePoint, Distance);
		ChargePoint -= Shot.Direction.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset;
	}
	ShowSupplyCue(ELSShellCueStyle::Charge, ChargePoint, Shot.Direction, 110, 1,
		ELSShellDetonationTrigger::Damage, FVector::ZeroVector, false, LSShellUsesArena(Shot) ? Reactive->GetPhysicalBody() : nullptr);
	SetReactionText(FString::Printf(TEXT("충전 %.0f · 저장 %.0f"), Accepted, Reactive->GetStoredCharge()));
	UE_LOG(LogTemp, Display, TEXT("SHELL_CHARGE Body=%llu Packet=%llu Activation=%llu Accepted=%.3f Stored=%.3f"),
		Reactive->BodyId, Packet.PacketId, Packet.ActivationSequence, Accepted, Reactive->GetStoredCharge());
	return Accepted;
}

TArray<ALSShellExperimentCoordinator::FQueuedReaction> ALSShellExperimentCoordinator::ReserveContactDischarges(const FQueuedReaction& Contact)
{
	if (!IsBattleActive() || !IsSupplyExperiment()) return {};
	TArray<FLSShellChargeGraphNode> Nodes;
	TMap<UPrimitiveComponent*, uint64> BodyIds;
	TMap<uint64, int32> Indices;
	TMap<uint64, TWeakObjectPtr<AActor>> Actors;
	TMap<uint64, FVector> Positions;
	TMap<uint64, ULSShellReactiveComponent*> Reactives;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>();
		const bool bAlive = ULSCombatTargetLibrary::IsCombatAlive(Actor);
		if (Reactive ? !Reactive->IsPhysicalBodyPresent() : !bAlive) continue;
		auto* Body = Reactive ? Reactive->GetPhysicalBody() : Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (!Body || !Body->IsQueryCollisionEnabled()) continue;
		uint64 Id = Reactive ? Reactive->BodyId : 0;
		if (!Reactive)
		{
			uint64& EndpointId = EndpointIdentities.FindOrAdd(Body);
			if (!EndpointId) EndpointId = ++NextEndpointId;
			Id = EndpointId;
		}
		if (!Id) continue;
		FLSShellChargeGraphNode Node;
		Node.BodyId = Id;
		Node.bRelay = Reactive && Reactive->bConductiveRelay;
		Node.bDamageEndpoint = bAlive && (!Reactive || !Reactive->bConductiveRelay || Actor->IsA<ALSShellExperimentEnemy>());
		Node.Store = Reactive && Reactive->bCanStoreCharge ? &Reactive->GetChargeStore() : nullptr;
		Indices.Add(Id, Nodes.Add(MoveTemp(Node)));
		Actors.Add(Id, Actor);
		Positions.Add(Id, Body->Bounds.Origin);
		BodyIds.Add(Body, Id);
		if (Reactive) Reactives.Add(Id, Reactive);
	}
	for (const auto& Entry : Reactives)
	{
		for (UPrimitiveComponent* OtherBody : Entry.Value->GetContactBodies())
		{
			const uint64* OtherId = BodyIds.Find(OtherBody);
			if (!OtherId || Entry.Value->IsContactInsulated(OtherBody)) continue;
			Nodes[Indices[Entry.Key]].Connections.AddUnique(*OtherId);
			Nodes[Indices[*OtherId]].Connections.AddUnique(Entry.Key);
		}
	}
	auto FindActorBodyId = [&BodyIds](AActor* Actor) -> uint64
	{
		if (!IsValid(Actor)) return 0;
		const auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>();
		auto* Body = Reactive ? Reactive->GetPhysicalBody() : Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		const uint64* Id = BodyIds.Find(Body);
		return Id ? *Id : 0;
	};
	const TArray<FLSShellChargeShare> Shares = FLSShellChargeGraph::ConsumeContact(Nodes,
		FindActorBodyId(Contact.Moving.Get()), FindActorBodyId(Contact.Other.Get()),
		Contact.DischargeBudget, Contact.ChargeSequence);
	if (Shares.IsEmpty()) return {};
	++DischargeCount;
	TArray<FQueuedReaction> Discharges;
	for (const FLSShellChargeShare& Share : Shares)
	{
		FQueuedReaction& Event = Discharges.AddDefaulted_GetRef();
		Event.ChargeShare = Share;
		Event.Other = Actors.FindRef(Share.RecipientBodyId);
		Event.Shot.RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		Event.Shot.SupplyTuning = SupplyTuning;
		Event.Shot.BaseTuning = Tuning;
		Event.Shot.Source = Share.Packet.Source;
		Event.Shot.SourceId = Share.Packet.SourceId;
		Event.Shot.ShotId = Share.Packet.ShotId;
		Event.Shot.Faction = Share.Packet.Faction;
		Event.Shot.bPlayerResource = Share.Packet.Provenance == ELSShellChargeProvenance::Player;
		Event.Shot.Origin = Contact.Hit.ImpactPoint;
		Event.Hit.ImpactPoint = Positions.FindRef(Share.RecipientBodyId);
		Event.GameTime = Contact.GameTime;
		for (uint64 BodyId : Share.PathBodyIds)
			if (const FVector* Position = Positions.Find(BodyId)) Event.ChargePath.Add(*Position);
	}
	return Discharges;
}

void ALSShellExperimentCoordinator::ResolveDischarge(const FQueuedReaction& Event)
{
	if (!IsBattleActive()) return;
	const FVector Direction = (Event.Hit.ImpactPoint - Event.Shot.Origin).GetSafeNormal();
	ApplyExperimentDamage(Event.Other.Get(), Event.ChargeShare.RequestedDamage, Event.Shot,
		Event.Hit.ImpactPoint, Direction, ELSShellDamageCause::Electric);
	ShowSupplyCue(ELSShellCueStyle::Discharge, Event.Hit.ImpactPoint, Direction, 100);
	for (int32 Index = 1; Index < Event.ChargePath.Num(); ++Index)
	{
		FLSShellCueSpec Cue;
		Cue.Style = ELSShellCueStyle::Discharge;
		Cue.Location = Event.ChargePath[Index];
		Cue.Origin = Event.ChargePath[Index - 1];
		Cue.bDrawLink = true;
		Cue.Color = FLinearColor(.45f, .65f, 2);
		Cue.Radius = 40;
		Cue.Strength = .4f;
		Cue.DurationSeconds = DischargeFeedback.DurationSeconds;
		Cue.FlashSeconds = DischargeFeedback.FlashSeconds;
		GetWorld()->SpawnActor<ALSShellDetonationCue>(Cue.Location, FRotator::ZeroRotator)->Initialize(Cue);
	}
	UE_LOG(LogTemp, Display, TEXT("SHELL_DISCHARGE Packet=%llu Carrier=%llu Target=%llu Amount=%.3f ContactTime=%.6f"),
		Event.ChargeShare.Packet.PacketId, Event.ChargeShare.Packet.CarrierBodyId,
		Event.ChargeShare.RecipientBodyId, Event.ChargeShare.RequestedDamage, Event.GameTime);
}

FString ALSShellExperimentCoordinator::GetMaterialStatusText() const
{
	return IsSupplyExperiment() && SupplyStage >= ELSShellSupplyStage::Materials
		? FString::Printf(TEXT("생성 철구 %.0f / %.0f kg · %d / %d · 조준 + G 회수"), GeneratedMaterials.GetLiveMassKg(),
			SupplyTuning.GeneratedMassCapKg, GeneratedMaterials.GetLiveBodyCount(), SupplyTuning.GeneratedBodyCap) : FString();
}

FString ALSShellExperimentCoordinator::GetEventText() const
{
	if (!IsSupplyExperiment()) return EventText;
	FString Text = EventText;
	for (TActorIterator<ALSShellSupplySchedule> It(GetWorld()); It; ++It)
	{
		const FString Supply = It->GetStatusText();
		if (!Supply.IsEmpty()) { if (!Text.IsEmpty()) Text += TEXT(" · "); Text += Supply; }
	}
	const ULSShellSiegeController* NextSiege = nullptr;
	int32 HeavyCount = 0;
	for (TActorIterator<ALSShellExperimentEnemy> It(GetWorld()); It; ++It)
		if (It->IsHeavy() && ULSCombatTargetLibrary::IsCombatAlive(*It) && It->Siege && It->Siege->Phase != ELSShellSiegePhase::Disabled)
		{
			++HeavyCount;
			const auto IsThreat = [](const ULSShellSiegeController* Siege)
			{ return Siege && (Siege->Phase == ELSShellSiegePhase::Loading || Siege->Phase == ELSShellSiegePhase::Reloading || Siege->Phase == ELSShellSiegePhase::Firing); };
			if (!NextSiege || (IsThreat(It->Siege) && !IsThreat(NextSiege)) ||
				(IsThreat(It->Siege) == IsThreat(NextSiege) && It->Siege->GetPhaseRemainingSeconds() < NextSiege->GetPhaseRemainingSeconds())) NextSiege = It->Siege;
		}
	if (NextSiege)
	{
		if (!Text.IsEmpty()) Text += TEXT("\n");
		Text += HeavyCount > 1 ? FString::Printf(TEXT("공성 %d기 · %s"), HeavyCount, *NextSiege->GetWarningText()) : NextSiege->GetWarningText();
	}
	return Text;
}

void ALSShellExperimentCoordinator::QueueCooling(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit)
{
	if (!IsBattleActive()) return;
	FQueuedReaction Event;
	Event.Kind = FQueuedReaction::EKind::Cooling;
	Event.Shot = Shot;
	Event.Hit = Hit;
	Event.Hit.ImpactPoint = Point;
	Reactions.Add(MoveTemp(Event));
}

void ALSShellExperimentCoordinator::ResolveCooling(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit)
{
	if (!IsBattleActive() || !LSShellUsesArena(Shot)) return;
	const auto& Arena = Shot.SupplyTuning.Arena;
	const float Seconds = Shot.ColdSeconds >= 0 ? Shot.ColdSeconds : Arena.CoolingSeconds * Shot.EffectScale;
	if (Seconds <= 0) return;
	const float Radius = Shot.bChild ? Arena.CoolingChildRadius : Arena.CoolingRadius;
	const FVector Center = Point + Hit.ImpactNormal.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset;
	TArray<TWeakObjectPtr<ULSShellReactiveComponent>> Targets;
	TSet<uint64> Seen;
	const auto AddTarget = [&](AActor* Actor)
	{
		auto* Reactive = IsValid(Actor) ? Actor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
		if (!Reactive || !Reactive->CanReceiveMomentum() || Seen.Contains(Reactive->BodyId)) return;
		Seen.Add(Reactive->BodyId);
		Targets.Add(Reactive);
	};
	// 직격 몸체를 먼저 처리하되 범위 반복에서 두 번째 냉각을 만들지 않는다.
	AddTarget(ResolveBodyTarget(Hit.GetActor()));
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (BlastDistance(*It, Center) < Radius && HasBlastLine(GetWorld(), *It, Center, nullptr, nullptr, nullptr, true)) AddTarget(*It);
	int32 Accepted = 0;
	for (const auto& Target : Targets)
		if (auto* Reactive = Target.Get(); Reactive && Reactive->CanReceiveMomentum())
			if (auto* Material = ULSShellMaterialStateComponent::GetOrCreate(Reactive->GetOwner()))
				if (Material->ApplyCooling(Seconds)) ++Accepted;
	ShowSupplyCue(ELSShellCueStyle::Cooling, Center, FVector::UpVector, Radius);
	SetReactionText(FString::Printf(TEXT("범위 냉각 · %d 몸체"), Accepted));
	UE_LOG(LogTemp, Display, TEXT("SHELL_ARENA_COOLING Shot=%llu Targets=%d Seconds=%.3f Point=%s"), Shot.ShotId, Accepted, Seconds, *Center.ToCompactString());
}

void ALSShellExperimentCoordinator::ResolveGatherFollowup(ELSShellModule Module, const FLSShellShot& Shot, const FLSShellGatherOutput& Output)
{
	if (!IsBattleActive() || !LSShellUsesArena(Shot)) return;
	++ModuleCount;
	SetReactionText(FString::Printf(TEXT("집결 → %s"), LSShellModuleLabel(Module)));
	const FVector Origin = Output.Center + FVector(0, 0, Shot.SupplyTuning.Arena.GatherFollowupHeight);
	if (Module == ELSShellModule::Blast)
	{
		FLSShellExplosion Explosion;
		Explosion.Location = Origin;
		Explosion.Shot = Shot;
		Explosion.Cause = ELSShellDamageCause::Module;
		Explosion.Damage = Shot.BaseTuning.BlastDamage * Shot.EffectScale;
		Explosion.Radius = Shot.BaseTuning.BlastRadius;
		for (const auto& Member : Output.Members) Explosion.IgnoredCoverBodyIds.AddUnique(Member.BodyId);
		QueueExplosion(Explosion);
	}
	else if (Module == ELSShellModule::Cooling)
	{
		FHitResult PointHit;
		PointHit.ImpactPoint = PointHit.Location = Origin;
		ResolveCooling(Origin, Shot, PointHit);
	}
	else if (Module == ELSShellModule::Thrust)
	{
		const FVector Direction = Shot.Direction.GetSafeNormal2D();
		TMap<uint64, float> GroupMass;
		for (const auto& Member : Output.Members)
			GroupMass.FindOrAdd(Member.AssemblyId ? Member.AssemblyId : Member.BodyId) += Member.MassKg;
		int32 Applied = 0;
		for (const auto& Member : Output.Members)
		{
			auto* Reactive = Member.Reactive.Get();
			if (!Reactive || Reactive->BodyId != Member.BodyId || !Reactive->IsPhysicalBodyPresent()) Reactive = RemappedBodyIdentities.FindRef(Member.BodyId).Get();
			if (!Reactive || !Reactive->CanReceiveMomentum()) continue;
			const float Mass = GroupMass.FindRef(Member.AssemblyId ? Member.AssemblyId : Member.BodyId);
			if (Mass <= 0 || Member.MassKg <= 0) continue;
			const float DeltaSpeed = FMath::Min(Shot.SupplyTuning.ThrustMomentum * Shot.EffectScale / Mass, Shot.SupplyTuning.ModuleMaximumDeltaSpeed);
			if (Reactive->ApplyMomentum(Direction * DeltaSpeed * Member.MassKg))
			{ Reactive->MarkPropelledByThrust(); Reactive->NotifyExternalForce(); ++Applied; }
		}
		ShowSupplyCue(ELSShellCueStyle::Thrust, Origin, Direction, ThrustCueRadius * 2);
		if (!Applied) SetTargetFailure(TEXT("모인 대상 없음"));
		UE_LOG(LogTemp, Display, TEXT("SHELL_ARENA_GROUP_THRUST Shot=%llu Members=%d Applied=%d Point=%s"), Shot.ShotId, Output.Members.Num(), Applied, *Origin.ToCompactString());
	}
}

void ALSShellExperimentCoordinator::ResolveArenaSurfaceContact(const FQueuedReaction& Event, ULSShellReactiveComponent* Moving, AActor* Surface)
{
	if (!IsArenaExperiment() || !Moving || !Moving->IsPhysicalBodyPresent() || !IsValid(Surface)) return;
	const uint64 ContactBodyId = Moving->BodyId;
	const float Speed = FMath::Abs(FVector::DotProduct(Event.Velocity - Event.OtherVelocity, Event.Hit.ImpactNormal));
	if (Speed <= Tuning.ImpactMinimumSpeed) return;
	const bool bLanding = Event.Hit.ImpactNormal.Z >= SupplyTuning.Arena.LandingNormalZ;
	if (bLanding && !Event.bThrustPropelled) return;
	FLSShellShot Environment;
	CaptureShotContext(Environment, Surface);
	Environment.bPlayerResource = false;
	Environment.Origin = Event.Hit.ImpactPoint;
	const float Damage = FMath::Min(SupplyTuning.MaximumImpactDamage, (Speed - Tuning.ImpactMinimumSpeed) * SupplyTuning.ImpactDamagePerSpeed);
	++ContactCount;
	LastContactActors = Moving->GetOwner()->GetName() + TEXT(" / ") + Surface->GetName();
	LastContactSpeed = Speed; LastContactGameTime = Event.GameTime; LastContactStepSeconds = Event.StepSeconds;
	const auto* Material = Moving->GetOwner()->FindComponentByClass<ULSShellMaterialStateComponent>();
	const bool bShatter = Material && Material->ShouldShatter(Speed);
	ApplyExperimentDamage(Moving->GetOwner(), Damage, Environment, Event.Hit.ImpactPoint, Event.Velocity, ELSShellDamageCause::Impact);
	auto* Current = ResolveCurrentBody(Moving);
	if (bShatter && Current && Current->IsPhysicalBodyPresent())
	{
		if (ULSCombatTargetLibrary::IsCombatAlive(Current->GetOwner()))
		{
			ApplyExperimentDamage(Current->GetOwner(), ReadHealth(Current->GetOwner()).CurrentHealth, Environment,
				Event.Hit.ImpactPoint, Event.Velocity, ELSShellDamageCause::Impact);
			Current = ResolveCurrentBody(Current);
		}
		if (Current && Current->IsPhysicalBodyPresent() && !ULSCombatTargetLibrary::IsCombatAlive(Current->GetOwner()))
		{ ShowSupplyCue(ELSShellCueStyle::Shatter, Event.Hit.ImpactPoint, Event.Hit.ImpactNormal, 180); Current->GetOwner()->Destroy(); }
	}
	else ShowSupplyCue(ELSShellCueStyle::Contact, Event.Hit.ImpactPoint, Event.Hit.ImpactNormal);
	UE_LOG(LogTemp, Display, TEXT("SHELL_ARENA_SURFACE BodyId=%llu Landing=%d Speed=%.2f Damage=%.2f"), ContactBodyId, bLanding, Speed, Damage);
}

void ALSShellExperimentCoordinator::ResolvePointThrust(FVector Point, const FLSShellShot& Shot)
{
	if (!IsBattleActive()) return;
	++ModuleCount;
	const FVector Direction = Shot.Direction.GetSafeNormal2D();
	if (Direction.IsNearlyZero()) { SetTargetFailure(TEXT("추진 방향 없음")); return; }
	struct FTarget
	{
		TWeakObjectPtr<ULSShellReactiveComponent> Reactive;
		float Weight;
		FVector Impulse;
	};
	TArray<FTarget> Targets;
	float SumWeights = 0;
	TSet<UPrimitiveComponent*> ShownBlockers;
	if (GetWorld()->GetTimeSeconds() > ShieldUntil) ShieldPoints.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		auto* Reactive = It->FindComponentByClass<ULSShellReactiveComponent>();
		if (!Reactive || !Reactive->CanReceiveMomentum()) continue;
		FVector Surface;
		double Distance;
		if (!LSObstacleGeometry::GetClosestBodyPoint(Reactive->GetPhysicalBody(), Point, Surface, Distance) ||
			Distance >= Shot.SupplyTuning.ThrustRadius) continue;
		FHitResult Cover;
		if (!HasBlastLine(GetWorld(), *It, Point, nullptr, &Cover))
		{
			if (UPrimitiveComponent* Blocker = Cover.GetComponent(); Blocker && !ShownBlockers.Contains(Blocker))
			{
				ShownBlockers.Add(Blocker);
				ShieldPoints.Add(Cover.ImpactPoint);
				ShieldUntil = GetWorld()->GetTimeSeconds() + .15f;
			}
			continue;
		}
		const float Weight = 1 - Distance / Shot.SupplyTuning.ThrustRadius;
		Targets.Add({Reactive, Weight, FVector::ZeroVector});
		SumWeights += Weight;
	}
	// 이 발동 중 몸체 상태가 바뀌어도 배정한 충격을 다시 나누지 않는다.
	for (FTarget& Target : Targets)
	{
		if (!Target.Reactive.IsValid() || !Target.Reactive->CanReceiveMomentum()) continue;
		Target.Impulse = (Direction * Shot.SupplyTuning.ThrustMomentum * Shot.EffectScale * Target.Weight /
			FMath::Max(1.f, SumWeights)).GetClampedToMaxSize(FMath::Max(0.f,
				Shot.SupplyTuning.ModuleMaximumDeltaSpeed) * Target.Reactive->GetPhysicsMassKg());
	}
	for (const FTarget& Target : Targets)
		if (Target.Reactive.IsValid() && Target.Reactive->ApplyMomentum(Target.Impulse))
		{
			if (LSShellUsesArena(Shot)) Target.Reactive->MarkPropelledByThrust();
			Target.Reactive->NotifyExternalForce();
		}
	ShowSupplyCue(ELSShellCueStyle::Thrust, Point, Direction);
	SetReactionText(TEXT("착탄점 추진"));
	ShowPulse(Point, Shot.SupplyTuning.ThrustRadius, FLinearColor(.2f, 1.4f, 2));
}

void ALSShellExperimentCoordinator::ResolveSupplyContact(const FQueuedReaction& Event)
{
	bool bRemapped = false;
	auto ResolveContactActor = [this, &bRemapped](TWeakObjectPtr<AActor> Original, uint64 BodyId, uint32 Generation) -> AActor*
	{
		if (AActor* Actor = Original.Get())
		{
			const auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>();
			if (!BodyId) return Reactive ? nullptr : Actor;
			if (Reactive && Reactive->BodyId == BodyId && Reactive->GetBodyGeneration() == Generation) return Actor;
		}
		auto* Replacement = RemappedBodyIdentities.FindRef(BodyId).Get();
		if (!BodyId || !Replacement || Replacement->BodyId != BodyId || Replacement->GetBodyGeneration() <= Generation ||
			!Replacement->IsPhysicalBodyPresent()) return nullptr;
		bRemapped = true;
		return Replacement->GetOwner();
	};
	AActor* A = ResolveContactActor(Event.Moving, Event.MovingBodyId, Event.MovingGeneration);
	AActor* B = ResolveContactActor(Event.Other, Event.OtherBodyId, Event.OtherGeneration);
	if (!IsValid(A) || !IsValid(B)) return;
	auto* RA = A->FindComponentByClass<ULSShellReactiveComponent>();
	auto* RB = B->FindComponentByClass<ULSShellReactiveComponent>();
	if (bRemapped)
	{
		auto* BodyA = RA ? RA->GetPhysicalBody() : Cast<UPrimitiveComponent>(A->GetRootComponent());
		auto* BodyB = RB ? RB->GetPhysicalBody() : Cast<UPrimitiveComponent>(B->GetRootComponent());
		// 명시적 교체라도 같은 접촉 식별값과 3cm 영역이 이어질 때만 원래 사건을 전달한다.
		if (!Event.ContactEpoch || !BodyA || !BodyB || !BodyA->IsQueryCollisionEnabled() || !BodyB->IsQueryCollisionEnabled() ||
			(RA && RA->GetContinuingContactEpoch(BodyB) != Event.ContactEpoch) ||
			(RB && RB->GetContinuingContactEpoch(BodyA) != Event.ContactEpoch) ||
			!BodyB->OverlapComponent(BodyA->GetComponentLocation(), BodyA->GetComponentQuat(), BodyA->GetCollisionShape(3))) return;
	}
	FQueuedReaction Contact = Event;
	Contact.Moving = A; Contact.Other = B;
	const float Speed = FMath::Abs(FVector::DotProduct(Event.Velocity - Event.OtherVelocity, Event.Hit.ImpactNormal));
	const bool bBodyA = ULSCombatTargetLibrary::IsCombatAlive(A) || (RA && RA->IsPhysicalBodyPresent());
	const bool bBodyB = ULSCombatTargetLibrary::IsCombatAlive(B) || (RB && RB->IsPhysicalBodyPresent());
	// 지면·벽은 피해/전도 종점이 아니지만 강한 실제 접촉은 폭약을 기폭할 수 있다.
	if (!bBodyA || !bBodyB)
	{
		if (Speed >= Tuning.ImpactMinimumSpeed)
		{
			if (bBodyA && RA && Speed >= RA->DetonationImpactThreshold) RA->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
			if (bBodyB && RB && Speed >= RB->DetonationImpactThreshold) RB->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
		}
		if (bBodyA && RA) ResolveArenaSurfaceContact(Event, RA, B);
		return;
	}
	const float MassA = RA ? RA->GetPhysicsMassKg() : SupplyTuning.ReferenceMassKg;
	const float MassB = RB ? RB->GetPhysicsMassKg() : SupplyTuning.ReferenceMassKg;
	if (IsArenaExperiment() && Event.bThrustPropelled && Speed >= Tuning.ImpactMinimumSpeed)
	{
		if (RA && RA->CanReceiveMomentum()) RA->MarkPropelledByThrust();
		if (RB && RB->CanReceiveMomentum()) RB->MarkPropelledByThrust();
	}
	const TArray<FQueuedReaction> Discharges = Speed >= Event.DischargeMinimumSpeed
		? ReserveContactDischarges(Contact) : TArray<FQueuedReaction>();
	if (Speed >= Tuning.ImpactMinimumSpeed)
	{
		if (RA && Speed >= RA->DetonationImpactThreshold) RA->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
		if (RB && Speed >= RB->DetonationImpactThreshold) RB->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
	}
	if (Speed >= ContactCueMinimumSpeed)
	{
		const float Weight = FMath::Sqrt(FMath::Max(MassA, MassB) / FMath::Max(1.f, ContactCueReferenceMassKg));
		const float SpeedScale = FMath::Clamp(Speed / FMath::Max(1.f, Tuning.ImpactMinimumSpeed), .1f, 1.f);
		ShowSupplyCue(ELSShellCueStyle::Contact, Event.Hit.ImpactPoint, Event.Hit.ImpactNormal, 0, Weight * SpeedScale);
	}
	// 느리고 무거운 실제 접촉도 들리게 하되 피해·기폭·캐릭터 밀림의 기존 임계값은 유지한다.
	if (Speed < Tuning.ImpactMinimumSpeed)
	{
		for (const FQueuedReaction& Discharge : Discharges) ResolveDischarge(Discharge);
		return;
	}
	const float DamagePerMass = (Speed - Tuning.ImpactMinimumSpeed) * SupplyTuning.ImpactDamagePerSpeed / SupplyTuning.ReferenceMassKg;
	FLSShellShot FromA, FromB;
	CaptureShotContext(FromA, A); CaptureShotContext(FromB, B);
	FromA.bPlayerResource = FromB.bPlayerResource = false;
	if (const auto* Origin = GeneratedMaterials.GetOrigin(A)) FromA = *Origin;
	if (const auto* Origin = GeneratedMaterials.GetOrigin(B)) FromB = *Origin;
	FromA.Origin = A->GetActorLocation();
	FromB.Origin = B->GetActorLocation();
	++ContactCount;
	LastContactActors = A->GetName() + TEXT(" / ") + B->GetName();
	LastContactSpeed = Speed; LastContactStepSeconds = Event.StepSeconds;
	LastContactGameTime = Event.GameTime;
	UE_LOG(LogTemp, Display, TEXT("SHELL_SUPPLY_CONTACT A=%s B=%s Speed=%.3f MassKg=%.3f/%.3f Step=%.6f Point=%s"),
		*A->GetName(), *B->GetName(), Speed, MassA, MassB, Event.StepSeconds, *Event.Hit.ImpactPoint.ToCompactString());
	ApplyExperimentDamage(B, FMath::Min(SupplyTuning.MaximumImpactDamage, DamagePerMass * MassA), FromA, Event.Hit.ImpactPoint, Event.Velocity, ELSShellDamageCause::Impact);
	ApplyExperimentDamage(A, FMath::Min(SupplyTuning.MaximumImpactDamage, DamagePerMass * MassB), FromB, Event.Hit.ImpactPoint, Event.OtherVelocity, ELSShellDamageCause::Impact);
	// 현재 몸체가 강체이면 솔버가 이미 접촉 운동량을 처리했다. 사망 후 교체한 강체에도 중복하지 않는다.
	const float Momentum = Speed * MassA * MassB / FMath::Max(1.f, MassA + MassB);
	if (auto* Current = ResolveCurrentBody(RA); Current && A->IsA<ACharacter>() && Current->GetPhysicalBody() &&
		!Current->GetPhysicalBody()->IsSimulatingPhysics()) Current->ApplyMomentum(Event.Hit.ImpactNormal * Momentum);
	if (auto* Current = ResolveCurrentBody(RB); Current && B->IsA<ACharacter>() && Current->GetPhysicalBody() &&
		!Current->GetPhysicalBody()->IsSimulatingPhysics()) Current->ApplyMomentum(-Event.Hit.ImpactNormal * Momentum);
	// 먼저 고정한 물리 접촉 피해를 처리하고, 남은 체력에 전격을 적용해 실제 기폭 원인을 보존한다.
	for (const FQueuedReaction& Discharge : Discharges) ResolveDischarge(Discharge);
	for (ULSShellReactiveComponent* Previous : {RA, RB})
	{
		auto* Current = ResolveCurrentBody(Previous);
		if (!Current || !Current->IsPhysicalBodyPresent()) continue;
		auto* Material = Current->GetOwner()->FindComponentByClass<ULSShellMaterialStateComponent>();
		if (!Material || !Material->ShouldShatter(Speed)) continue;
		AActor* BodyOwner = Current->GetOwner();
		if (ULSCombatTargetLibrary::IsCombatAlive(BodyOwner))
		{
			ApplyExperimentDamage(BodyOwner, ReadHealth(BodyOwner).CurrentHealth, FromA, Event.Hit.ImpactPoint, Event.Velocity, ELSShellDamageCause::Impact);
			Current = ResolveCurrentBody(Current);
		}
		if (Current && Current->IsPhysicalBodyPresent() && !ULSCombatTargetLibrary::IsCombatAlive(Current->GetOwner()))
		{
			ShowSupplyCue(ELSShellCueStyle::Shatter, Current->GetOwner()->GetActorLocation(), Event.Hit.ImpactNormal, 180);
			Current->GetOwner()->Destroy();
		}
	}
}

void ALSShellExperimentCoordinator::ResolveContact(const FQueuedReaction& Event)
{
	if (IsSupplyExperiment()) { ResolveSupplyContact(Event); return; }
	AActor* A = Event.Moving.Get(); AActor* B = Event.Other.Get();
	if (!A || !B) return;
	auto* RA = A->FindComponentByClass<ULSShellReactiveComponent>();
	auto* RB = B->FindComponentByClass<ULSShellReactiveComponent>();
	const float Speed = FMath::Abs(FVector::DotProduct(Event.Velocity - Event.OtherVelocity, Event.Hit.ImpactNormal));
	if (Speed < Tuning.ImpactMinimumSpeed) return;
	if (RA && Speed >= RA->DetonationImpactThreshold) RA->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
	if (RB && Speed >= RB->DetonationImpactThreshold) RB->RequestDetonationWithCause(ELSShellDetonationTrigger::Impact, Event.Hit.ImpactPoint);
	const bool bAliveA = ULSCombatTargetLibrary::IsCombatAlive(A), bAliveB = ULSCombatTargetLibrary::IsCombatAlive(B);
	// 일반 병력의 착지에는 별도 낙하 피해를 더하지 않는다.
	if (!bAliveA || !bAliveB) return;
	++ContactCount;
	SetReactionText(TEXT("충돌"));
	ShowPulse(Event.Hit.ImpactPoint, 170, FLinearColor(3, 3, 2));
	LastContactActors = A->GetName() + TEXT(" / ") + B->GetName();
	LastContactSpeed = Speed;
	LastContactStepSeconds = Event.StepSeconds;
	FLSShellShot Shot; Shot.Source = A; Shot.Origin = A->GetActorLocation();
	if (const auto* Faction = Cast<ILSFactionInterface>(A)) Shot.Faction = Faction->GetFaction();
	const float MassA = RA ? RA->GetMass() : 1, MassB = RB ? RB->GetMass() : 1;
	const float Amount = (Speed - Tuning.ImpactMinimumSpeed) * Tuning.ImpactDamagePerSpeed;
	FLSShellShot OtherShot;
	OtherShot.Source = B; OtherShot.Origin = B->GetActorLocation();
	if (const auto* Faction = Cast<ILSFactionInterface>(B)) OtherShot.Faction = Faction->GetFaction();
	ApplyExperimentDamage(B, Amount * MassA / FMath::Max(1.f, RB ? RB->GetMovementResistance() : 1), Shot, Event.Hit.ImpactPoint, Event.Velocity, ELSShellDamageCause::Impact);
	ApplyExperimentDamage(A, Amount * MassB / FMath::Max(1.f, RA ? RA->GetMovementResistance() : 1), OtherShot, Event.Hit.ImpactPoint, Event.OtherVelocity, ELSShellDamageCause::Impact);
	if (RB && ULSCombatTargetLibrary::IsCombatAlive(B)) RB->ApplyImpulseVelocity(Event.Velocity * (1.5f * MassA / (MassA + MassB)));
}

void ALSShellExperimentCoordinator::ExecuteModule(ELSShellModule Module, FVector Point, const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact, ALSShellEffectInstance* Instance)
{
	if (!IsBattleActive()) return;
	if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && Shot.bChild &&
		((Module == ELSShellModule::Electric && Shot.EffectScale <= 0 && Shot.ChargePayload.IsEmpty()) ||
		(Module == ELSShellModule::Gravity && !LSShellUsesArena(Shot) && Shot.SupplyTuning.GravityIntegratedMomentum <= 0))) return;
	++ModuleCount;
	SetReactionText(FString::Printf(TEXT("%s 발동"), LSShellModuleLabel(Module)));
	if (Module == ELSShellModule::Blast)
	{
		FLSShellExplosion Explosion; Explosion.Location = Point; Explosion.Shot = Shot; Explosion.Cause = ELSShellDamageCause::Module;
		const FLSShellTuning& Settings = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne ? Shot.BaseTuning : Tuning;
		if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
			Explosion.Location += Hit.ImpactNormal.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset;
		Explosion.Damage = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne ? Settings.BlastDamage * Shot.EffectScale :
			(Shot.bChild ? 50 : Settings.BlastDamage * Shot.EffectScale);
		Explosion.Radius = Shot.bChild ? 300 : Settings.BlastRadius;
		Explosion.Velocity = Tuning.BlastVelocity * Shot.EffectScale;
		QueueExplosion(Explosion);
	}
	else if (Module == ELSShellModule::Thrust && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{
		AActor* Actor = ResolveBodyTarget(Hit.GetActor());
		auto* Reactive = IsValid(Actor) ? Actor->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
		const FVector Direction = Shot.Direction.GetSafeNormal2D();
		if (Direction.IsNearlyZero())
			SetTargetFailure(TEXT("추진 방향 없음"));
		else if (!Reactive || !Reactive->ApplyMomentum((Direction * Shot.SupplyTuning.ThrustMomentum * Shot.EffectScale)
			.GetClampedToMaxSize(FMath::Max(0.f, Shot.SupplyTuning.ModuleMaximumDeltaSpeed) * Reactive->GetPhysicsMassKg())))
			NotifyMissingTarget();
		else
		{
			if (LSShellUsesArena(Shot)) Reactive->MarkPropelledByThrust();
			const UPrimitiveComponent* Body = Reactive->GetPhysicalBody();
			LastThrustGameTime = GetWorld()->GetTimeSeconds();
			Reactive->NotifyExternalForce();
			const FVector Center = Body->IsSimulatingPhysics() ? Body->GetCenterOfMass() : Body->Bounds.Origin;
			// 회전한 저장 접점 대신 현재 몸체의 입사 쪽 표면에 화살표를 놓는다.
			const double OutsideOffset = FVector::Distance(Center, Body->Bounds.Origin) + Body->Bounds.SphereRadius + 1;
			const FVector QueryPoint = Center - Direction * OutsideOffset;
			FVector CuePoint;
			double Distance;
			if (LSObstacleGeometry::GetClosestBodyPoint(Body, QueryPoint, CuePoint, Distance) && Distance > 0)
				ShowSupplyCue(ELSShellCueStyle::Thrust, CuePoint, Direction);
			else
			{
				SetReactionText(TEXT("추진"));
				if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Center,
					FMath::Clamp(ThrustFeedback.Strength, .1f, 1.5f), ThrustFeedback.PitchMultiplier);
			}
		}
	}
	else if (Module == ELSShellModule::Electric && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{
		if (Shot.ChargePayload.IsEmpty()) ApplyCharge(ResolveBodyTarget(Hit.GetActor()), Shot, Shot.SupplyTuning.ElectricCharge * Shot.EffectScale);
		else
		{
			AActor* Target = ResolveBodyTarget(Hit.GetActor());
			auto* Reactive = IsValid(Target) ? Target->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
			float Incoming = 0, Accepted = 0;
			for (auto Packet : Shot.ChargePayload)
			{
				Incoming += Packet.Amount;
				if (!Reactive || !Reactive->IsPhysicalBodyPresent()) continue;
				Packet.ActivationSequence = ++ChargeContactSequence;
				Accepted += Reactive->AddCharge(Packet, Shot.SupplyTuning.ChargeCapacity);
			}
			if (Accepted + UE_KINDA_SMALL_NUMBER < Incoming)
			{
				if (LSShellUsesArena(Shot) && Accepted <= UE_KINDA_SMALL_NUMBER)
					NotifyChargeFailure(Point, TEXT("전하 소산 · 저장 불가"));
				else SetTargetFailure(Accepted > 0 ? TEXT("전하 일부 소산 · 저장 한도") : TEXT("전하 소산 · 저장 불가"));
			}
			if (LSShellUsesArena(Shot) && Accepted > UE_KINDA_SMALL_NUMBER)
				ShowSupplyCue(ELSShellCueStyle::Charge, Point, Shot.Direction, 110, 1,
					ELSShellDetonationTrigger::Damage, FVector::ZeroVector, false, Reactive->GetPhysicalBody());
		}
	}
	else if (Module == ELSShellModule::Gravity)
	{
		const FVector Center = Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne ? Point + Hit.ImpactNormal.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset : Point;
		auto* Field = GetWorld()->SpawnActor<ALSShellGravityField>(Center, FRotator::ZeroRotator);
		if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne) Field->InitializeSupply(this, Shot);
		else Field->Initialize(this, Tuning.GravityRadius, Tuning.GravitySeconds, Tuning.GravityAcceleration * Shot.EffectScale);
		if (Instance) Instance->SetSourceGravityField(Field);
	}
	else if (Module == ELSShellModule::Iron && Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{
		FString Error;
		if (auto* Generated = GeneratedMaterials.GenerateIron(GetWorld(), Hit, Shot, Error))
		{ if (Instance) Instance->RetargetGeneratedBody(Generated); }
		else
		{
			SetTargetFailure(Error);
			if (Instance) Instance->CancelGeneratedFollowup();
		}
	}
	else if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne && (Module == ELSShellModule::Adhesion || Module == ELSShellModule::Cooling))
	{
		if (LSShellUsesArena(Shot) && Module == ELSShellModule::Cooling) { QueueCooling(Point, Shot, Hit); return; }
		AActor* Target = ResolveBodyTarget(Hit.GetActor());
		const auto* Reactive = IsValid(Target) ? Target->FindComponentByClass<ULSShellReactiveComponent>() : nullptr;
		if (!Reactive || !Reactive->CanReceiveMomentum()) { NotifyMissingTarget(); return; }
		auto* Material = ULSShellMaterialStateComponent::GetOrCreate(Target);
		if (!Material) { NotifyMissingTarget(); return; }
		if (Module == ELSShellModule::Cooling) { if (!Material->ApplyCooling(Shot.ColdSeconds >= 0 ? Shot.ColdSeconds : 4 * Shot.EffectScale)) NotifyMissingTarget(); }
		else if (Material->ApplyAdhesion(Shot.AdhesionSlots >= 0 ? Shot.AdhesionSlots : 3, Shot.EffectScale) == 0) NotifyMissingTarget();
	}
	else if (Module == ELSShellModule::Split && !Shot.bChild) SpawnChildren(Point, Shot, Hit, bCombatImpact, Instance);
}

void ALSShellExperimentCoordinator::SpawnChildren(FVector Point, const FLSShellShot& Shot, const FHitResult& Hit, bool bCombatImpact, ALSShellEffectInstance* Instance)
{
	if (Shot.RuleProfile == ELSShellRuleProfile::SupplyStageOne)
	{ FLSShellSplitRuntime::Execute(*this, Point, Shot, Hit, bCombatImpact, Instance); return; }
	FVector Basis = Shot.Direction;
	if (!bCombatImpact) Basis = FMath::GetReflectionVector(Basis, Hit.ImpactNormal);
	Basis = Basis.GetSafeNormal2D();
	if (Basis.IsNearlyZero()) Basis = Shot.Direction.GetSafeNormal2D();
	if (Basis.IsNearlyZero()) Basis = FVector::ForwardVector;
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	const float Ground = Settings ? Settings->GetGroundHeight() : Point.Z;
	// 몸체가 회전해도 입사 방향의 반대쪽에서 시작해 실제 몸체를 다시 검사한다.
	const FVector Outward = bCombatImpact ? -Basis : Hit.ImpactNormal.GetSafeNormal();
	const FVector Start = Point + Outward * 14;
	for (float Angle : {-40.f, 0.f, 40.f})
	{
		const FVector Planar = Basis.RotateAngleAxis(Angle, FVector::UpVector);
		const FVector GroundTarget(Point.X + Planar.X * 600, Point.Y + Planar.Y * 600, Ground + LSShellProjectileRadius - 0.1f);
		FLSShellShot Child = Shot;
		Child.bChild = true;
		Child.bInheritedModule = Shot.First == ELSShellModule::Split;
		Child.First = Shot.Second;
		Child.Damage = 20;
		Child.EffectScale = 0.5f;
		Child.Origin = Point;
		Child.Direction = (GroundTarget - Start).GetSafeNormal();
		FHitResult SpawnHit;
		if (ALSShellProjectile::SweepFlight(GetWorld(), Point, Start, Shot.Source.Get(), SpawnHit, Hit.GetComponent()))
			QueueShellHit(Child, SpawnHit);
		else
		{
			auto* Projectile = GetWorld()->SpawnActor<ALSShellProjectile>(Start, Child.Direction.Rotation());
			Projectile->Launch(this, Child, Start, Child.Direction);
		}
	}
}

void ALSShellExperimentCoordinator::SetComposing(bool bOpen)
{
	if (bOpen && !IsBattleActive()) return;
	if (bOpen == bComposing) return;
	if (bOpen) { PendingFirst = First; PendingSecond = Second; }
	else { First = PendingFirst; Second = PendingSecond; }
	bComposing = bOpen;
	UGameplayStatics::SetGlobalTimeDilation(this, bOpen ? 0.2f : 1.f);
}

void ALSShellExperimentCoordinator::SwapModules()
{
	if (IsBattleActive() && bComposing) Swap(PendingFirst, PendingSecond);
}

void ALSShellExperimentCoordinator::CancelCompose()
{
	PendingFirst = First;
	PendingSecond = Second;
	bComposing = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
}

ALSShellExperimentEnemy* ALSShellExperimentCoordinator::FindLivingHeavy() const
{
	for (TActorIterator<ALSShellExperimentEnemy> It(GetWorld()); It; ++It)
		if (It->IsHeavy() && ULSCombatTargetLibrary::IsCombatAlive(*It)) return *It;
	return nullptr;
}

bool ALSShellExperimentCoordinator::GetConvoyEntries(TArray<FVector>& Entries) const
{
	Entries.Reset();
	if (!CarrierClass || ConvoySpawnPoints.Num() != 2) return false;
	const ALSShellExperimentEnemy* Definition = CarrierClass->GetDefaultObject<ALSShellExperimentEnemy>();
	if (Definition->EnemyKind != ELSShellEnemyKind::Carrier) return false;
	const UCapsuleComponent* Capsule = Definition->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	FNavAgentProperties Agent = Definition->GetNavAgentPropertiesRef();
	Agent.AgentRadius = Radius;
	Agent.AgentHeight = HalfHeight * 2;
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Agent, ConvoyApproachPoint) : nullptr;
	if (!Data) return false;
	FNavLocation Goal;
	if (!Nav->ProjectPointToNavigation(ConvoyApproachPoint, Goal, FVector(100, 100, 300), Data)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellConvoyEntry), false, this);
	for (const FVector& Desired : ConvoySpawnPoints)
	{
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Desired, Projected, FVector(50, 50, 300), Data)) return false;
		const FVector Center = Projected.Location + FVector(0, 0, HalfHeight + 2);
		if (GetWorld()->OverlapBlockingTestByChannel(Center, FQuat::Identity, LSCollisionChannels::Unit,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Query)) return false;
		FPathFindingQuery PathQuery(nullptr, *Data, Projected.Location, Goal.Location);
		PathQuery.SetAllowPartialPaths(false);
		if (!Nav->TestPathSync(PathQuery, EPathFindingMode::Regular)) return false;
		Entries.Add(Center);
	}
	return FVector::Dist2D(Entries[0], Entries[1]) > Radius * 2;
}

void ALSShellExperimentCoordinator::UpdateEvent()
{
	if (!bEnableEvents || !IsBattleActive() || !ULSCombatTargetLibrary::IsCombatAlive(PlayerCastle) ||
		!ULSCombatTargetLibrary::IsCombatAlive(CoreOutpost)) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (EventTextUntil > 0 && Now >= EventTextUntil) EventText.Empty();
	if (!bEventAttempted && Now >= EventCandidateTime)
	{
		bEventAttempted = true;
		TArray<EEvent> Candidates;
		TArray<FVector> Entries;
		if (GetConvoyEntries(Entries)) Candidates.Add(EEvent::Convoy);
		if (FindLivingHeavy()) Candidates.Add(EEvent::Offensive);
		if (Candidates.IsEmpty()) return;
		PendingEvent = Candidates[FMath::RandHelper(Candidates.Num())];
		EventHeavy = FindLivingHeavy();
		EventWarningUntil = Now + 3;
		const FVector WarningPoint = PendingEvent == EEvent::Convoy ? Entries[0] : EventHeavy->GetActorLocation();
		ShowPulse(WarningPoint, 550, FLinearColor(0.1f, 2, 3), true);
		if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, WarningPoint, 0.7f, 1.8f);
	}
	if (PendingEvent == EEvent::None) return;
	const bool bConvoy = PendingEvent == EEvent::Convoy;
	EventText = FString::Printf(TEXT("%s · %.1f초 후"), bConvoy ? TEXT("수송대 경고 — 우측 상·하단 진입") : TEXT("중장갑 병기 공세 경고"), FMath::Max(0.f, EventWarningUntil - Now));
	if (Now < EventWarningUntil) return;
	TArray<FVector> Entries;
	if (bConvoy && GetConvoyEntries(Entries))
	{
		for (const FVector& Point : Entries)
			GetWorld()->SpawnActor<ALSShellExperimentEnemy>(CarrierClass, Point, FRotator(0, 180, 0));
		bEventExecuted = true;
		EventText = TEXT("폭발물 수송대 진입 — 운반차 2기");
	}
	else if (!bConvoy && EventHeavy.IsValid() && ULSCombatTargetLibrary::IsCombatAlive(EventHeavy.Get()))
	{
		EventHeavy->GetCharacterMovement()->MaxWalkSpeed *= 1.5f;
		FLSBasicAttackSettings Attack = EventHeavy->BasicAttack->GetAttackSettings();
		Attack.Interval *= 0.5f;
		EventHeavy->BasicAttack->ConfigureAttack(Attack);
		bEventExecuted = true;
		EventText = TEXT("중장갑 병기 공세 — 이동·포격 가속");
	}
	else EventText = TEXT("사건 취소 — 발생 조건 소멸");
	EventTextUntil = Now + 4;
	PendingEvent = EEvent::None;
	EventHeavy.Reset();
}

void ALSShellExperimentCoordinator::SelectModule(int32 Slot, ELSShellModule Module)
{
	if (!IsBattleActive() || !bComposing || Slot < 0 || Slot > 1 || !CanSelectModule(Module)) return;
	ELSShellModule& Selected = Slot == 0 ? PendingFirst : PendingSecond;
	ELSShellModule& Other = Slot == 0 ? PendingSecond : PendingFirst;
	if (Other == Module) Swap(Selected, Other); else Selected = Module;
}

ELSShellModule ALSShellExperimentCoordinator::GetModule(int32 Slot) const { return bComposing ? (Slot == 0 ? PendingFirst : PendingSecond) : (Slot == 0 ? First : Second); }
float ALSShellExperimentCoordinator::GetReloadFraction() const { return GetWorld() ? FMath::Clamp(1 - (NextFireTime - GetWorld()->GetTimeSeconds()) / Tuning.ReloadSeconds, 0.f, 1.f) : 0; }
bool ALSShellExperimentCoordinator::CanManualTrigger() const { return IsBattleActive() && ManualTarget.IsValid() && ManualTarget->IsPending(); }
void ALSShellExperimentCoordinator::TriggerPending() { if (CanManualTrigger()) ManualTarget->ActivateSecond(); }
void ALSShellExperimentCoordinator::SetManualTarget(ALSShellEffectInstance* Instance) { ManualTarget = Instance; }
void ALSShellExperimentCoordinator::ClearManualTarget(ALSShellEffectInstance* Instance) { if (ManualTarget == Instance) ManualTarget.Reset(); }
void ALSShellExperimentCoordinator::NotifyHeavyDefeated() { if (IsBattleActive()) bSplitUnlocked = true; }
FLSHealthSnapshot ALSShellExperimentCoordinator::GetCastleHealth() const { return ReadHealth(PlayerCastle); }
FLSHealthSnapshot ALSShellExperimentCoordinator::GetCoreHealth() const { return ReadHealth(CoreOutpost); }

void ALSShellExperimentCoordinator::CaptureShotContext(FLSShellShot& Shot, AActor* Source) const
{
	Shot.RuleProfile = RuleProfile;
	Shot.BaseTuning = Tuning;
	Shot.SupplyTuning = SupplyTuning;
	Shot.ShotId = ++NextShotId;
	Shot.Source = Source ? Source : PlayerCastle.Get();
	Shot.bPlayerResource = Shot.Source.Get() == PlayerCastle.Get();
	if (const auto* Remapped = RemappedSourceIdentities.Find(Shot.Source))
	{
		Shot.SourceId = Remapped->SourceId;
		Shot.Faction = Remapped->Faction;
		return;
	}
	uint64& Identity = SourceIdentities.FindOrAdd(Shot.Source);
	if (Identity == 0) Identity = ++NextSourceId;
	Shot.SourceId = Identity;
	if (const auto* Faction = Cast<ILSFactionInterface>(Shot.Source.Get())) Shot.Faction = Faction->GetFaction();
}

bool ALSShellExperimentCoordinator::CanSelectModule(ELSShellModule Module) const
{
	if (IsSupplyExperiment()) return AvailableModules.Contains(Module) && (Module != ELSShellModule::Split || bSplitUnlocked);
	return Module == ELSShellModule::Gravity || Module == ELSShellModule::Blast || (Module == ELSShellModule::Split && bSplitUnlocked);
}

void ALSShellExperimentCoordinator::SetTargetFailure(const FString& Text)
{
	TargetFailureText = Text;
	TargetFailureUntil = GetWorld()->GetTimeSeconds() + 1;
}

void ALSShellExperimentCoordinator::NotifyMissingTarget() { SetTargetFailure(TEXT("대상 없음")); }
FString ALSShellExperimentCoordinator::GetTargetFailureText() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() <= TargetFailureUntil ? TargetFailureText : FString();
}

bool ALSShellExperimentCoordinator::GetPendingEffectLocation(FVector& OutLocation) const
{
	if (!CanManualTrigger()) return false;
	OutLocation = ManualTarget->GetActorLocation();
	return true;
}

TArray<FVector> ALSShellExperimentCoordinator::GetShieldPoints() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() <= ShieldUntil ? ShieldPoints : TArray<FVector>();
}

void ALSShellExperimentCoordinator::SetReactionText(const FString& Text, int32 Priority)
{
	if (IsSupplyExperiment() && GetWorld()->GetTimeSeconds() < ReactionUntil && Priority < ReactionPriority) return;
	ReactionText = Text;
	ReactionPriority = Priority;
	ReactionUntil = GetWorld()->GetTimeSeconds() + 3;
}

FString ALSShellExperimentCoordinator::GetReactionText() const
{
	return GetWorld()->GetTimeSeconds() <= ReactionUntil ? ReactionText : FString();
}

FString ALSShellExperimentCoordinator::GetPendingEffectText() const
{
	if (!CanManualTrigger()) return FString();
	return FString::Printf(TEXT("%s까지 %.1f초 · Space로 즉시 발동"),
		LSShellModuleLabel(ManualTarget->GetPendingModule()), ManualTarget->GetPendingSeconds());
}

FString ALSShellExperimentCoordinator::GetStatusText() const
{
	if (!ConfigurationError.IsEmpty()) return ConfigurationError;
	if (const auto* State = GetWorld()->GetGameState<ALSGameState>(); State && State->HasBattleEnded())
		return LSBattleOutcome::IsVictory(State->GetBattleOutcome()) ? TEXT("승리 — 중앙 동력로 파괴") : TEXT("패배 — 요새 파괴");
	return GetReloadFraction() >= 1 ? TEXT("발사 준비") : TEXT("재장전 중");
}

void ALSShellExperimentCoordinator::ShowPulse(FVector Point, float Radius, FLinearColor Color, bool bLarge)
{
	if (auto* Pulse = GetWorld()->SpawnActor<ALSShellVisualPulse>(Point, FRotator::ZeroRotator)) Pulse->Initialize(Radius, Color, bLarge ? 0.7f : 0.35f);
}

void ALSShellExperimentCoordinator::ShowSupplyCue(ELSShellCueStyle Style, FVector Point, FVector Direction, float Radius, float StrengthScale,
	ELSShellDetonationTrigger Trigger, FVector TriggerOrigin, bool bHasTriggerOrigin, UPrimitiveComponent* TargetBody)
{
	FLSShellCueSpec Cue;
	Cue.Style = Style; Cue.Location = Point; Cue.Direction = Direction;
	Cue.TargetBody = TargetBody;
	Cue.Sound = ImpactSound;
	const FLSShellFeedbackTuning* Feedback = nullptr;
	switch (Style)
	{
	case ELSShellCueStyle::Thrust:
		Cue.Caption = TEXT("PUSH"); Cue.Color = FLinearColor(.05f, .8f, 1);
		Cue.Radius = ThrustCueRadius; Feedback = &ThrustFeedback;
		SetReactionText(TEXT("추진"));
		break;
	case ELSShellCueStyle::Contact:
		Cue.Caption = TEXT("HIT"); Cue.Color = FLinearColor(1, .9f, .65f);
		Cue.Radius = ContactCueRadius; Feedback = &ContactFeedback;
		SetReactionText(TEXT("충돌"));
		break;
	case ELSShellCueStyle::Charge:
		Cue.Caption = TEXT("CHARGE"); Cue.Color = FLinearColor(.5f, .3f, 1.5f);
		Cue.Radius = Radius; Feedback = &ChargeFeedback;
		SetReactionText(TEXT("충전"));
		break;
	case ELSShellCueStyle::Discharge:
		Cue.Caption = TEXT("ZAP"); Cue.Color = FLinearColor(.4f, .7f, 2);
		Cue.Radius = Radius; Feedback = &DischargeFeedback;
		SetReactionText(TEXT("접촉 방전"));
		break;
	case ELSShellCueStyle::Cooling:
		Cue.Caption = TEXT("냉각"); Cue.Color = FLinearColor(.65f, 1.5f, 2);
		Cue.Radius = Radius; Feedback = &CoolingFeedback;
		break;
	case ELSShellCueStyle::Failure:
		Cue.Caption = GetTargetFailureText(); Cue.Color = FLinearColor(1, .2f, .1f);
		Cue.Radius = Radius; Feedback = &FailureFeedback;
		break;
	case ELSShellCueStyle::Shatter:
		if (!IsArenaExperiment()) return;
		Cue.Caption = TEXT("파쇄"); Cue.Color = FLinearColor(.65f, 1.3f, 2);
		Cue.Radius = Radius; Feedback = &ContactFeedback;
		SetReactionText(TEXT("얼음 파쇄"));
		break;
	case ELSShellCueStyle::Blast:
		Cue.Caption = TEXT("BLAST"); Cue.Color = FLinearColor(1, .45f, .05f);
		Cue.Radius = Radius; Feedback = &BlastFeedback;
		Cue.Sound = ExplosionSound;
		SetReactionText(TEXT("포탄 폭발"));
		break;
	case ELSShellCueStyle::Chain:
		Cue.Caption = TEXT("BOOM"); Cue.Color = FLinearColor(1, .12f, .025f);
		Cue.Radius = Radius; Feedback = &ChainFeedback;
		Cue.Sound = ExplosionSound;
		Cue.Origin = TriggerOrigin; Cue.bDrawLink = bHasTriggerOrigin;
		switch (Trigger)
		{
		case ELSShellDetonationTrigger::Shell:
			Cue.Caption = TEXT("SHOT > BOOM"); SetReactionText(TEXT("직격 → 물체 기폭"), 1); break;
		case ELSShellDetonationTrigger::ModuleBlast:
			Cue.Caption = TEXT("BLAST > BOOM"); SetReactionText(TEXT("포탄 폭발 → 물체 기폭"), 1); break;
		case ELSShellDetonationTrigger::Impact:
			Cue.Caption = TEXT("HIT > BOOM"); SetReactionText(TEXT("충돌 → 물체 기폭"), 1); break;
		case ELSShellDetonationTrigger::Chain:
			Cue.Caption = TEXT("BOOM > BOOM"); SetReactionText(TEXT("주변 기폭 → 연쇄 기폭"), 1); break;
		case ELSShellDetonationTrigger::Discharge:
			Cue.Caption = TEXT("ZAP > BOOM"); SetReactionText(TEXT("접촉 방전 → 물체 기폭"), 1); break;
		default: SetReactionText(TEXT("피해 → 물체 기폭"), 1); break;
		}
		break;
	default: return;
	}
	Cue.Strength = Feedback->Strength * StrengthScale;
	Cue.DurationSeconds = Feedback->DurationSeconds;
	Cue.FlashSeconds = Feedback->FlashSeconds;
	Cue.PitchMultiplier = Feedback->PitchMultiplier;
	Cue.CaptionHeight = Feedback->CaptionHeight;
	Cue.LightRadius = Feedback->LightRadius;
	auto* VisualCue = GetWorld()->SpawnActor<ALSShellDetonationCue>(Point, FRotator::ZeroRotator);
	if (VisualCue) VisualCue->Initialize(Cue);
}

void ALSShellExperimentCoordinator::OnBattleEnded(ELSBattleOutcome Outcome)
{
	bStopped = true;
	Reactions.Reset(); ManualTarget.Reset();
	PendingEvent = EEvent::None; EventHeavy.Reset(); EventText.Empty();
	bComposing = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1);
	if (LSBattleOutcome::IsVictory(Outcome)) ShowDestroyedStructure(CoreOutpost);
	if (Outcome == ELSBattleOutcome::Defeat || Outcome == ELSBattleOutcome::MutualDestruction)
	{
		LauncherVisual->Hide();
		ShowDestroyedStructure(PlayerCastle);
	}
}

void ALSShellExperimentCoordinator::ShowDestroyedStructure(AActor* Target)
{
	if (!IsValid(Target)) return;
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Target);
	for (UStaticMeshComponent* Mesh : Meshes) Mesh->SetVisibility(false, true);
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	const float Ground = Settings ? Settings->GetGroundHeight() : 0;
	const FVector Origin(Target->GetActorLocation().X, Target->GetActorLocation().Y, Ground);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
	UMaterialInstanceDynamic* Material = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Material) Material->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor(0.055f, 0.045f, 0.04f));
	// 정적인 파괴 외형만 남긴다. 잔해에 충돌·피해·물리 시뮬레이션을 추가하지 않는다.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		auto* Wreck = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Wreck);
		Wreck->SetupAttachment(RootComponent);
		Wreck->SetCanEverAffectNavigation(false);
		Wreck->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wreck->SetStaticMesh(Cube);
		if (Material) Wreck->SetMaterial(0, Material);
		Wreck->RegisterComponent();
		Wreck->SetWorldLocation(Origin + FVector(Index == 0 ? -180 : 160, Index == 1 ? 190 : -110, 60));
		Wreck->SetWorldRotation(FRotator(0, Index * 28, 0));
		Wreck->SetWorldScale3D(FVector(4.4f, 4.4f, 1.2f));
	}
	FLSShellCueSpec Cue;
	Cue.Location = Origin + FVector(0, 0, 120);
	Cue.Radius = 1400; Cue.Strength = 2;
	Cue.Caption = Target == CoreOutpost ? TEXT("CORE DOWN") : TEXT("CASTLE DOWN");
	Cue.Sound = ExplosionSound;
	auto* Visual = GetWorld()->SpawnActor<ALSShellDetonationCue>(Cue.Location, FRotator::ZeroRotator);
	Visual->Initialize(Cue);
}

void ALSShellExperimentCoordinator::RestartExperiment() { UGameplayStatics::SetGlobalTimeDilation(this, 1); UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true))); }
void ALSShellExperimentCoordinator::EndPlay(const EEndPlayReason::Type Reason)
{
	FWorldDelegates::OnWorldPostActorTick.RemoveAll(this);
	UGameplayStatics::SetGlobalTimeDilation(this, 1);
	Super::EndPlay(Reason);
}
