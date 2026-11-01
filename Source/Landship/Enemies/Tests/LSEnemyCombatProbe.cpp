#include "Enemies/Tests/LSEnemyCombatProbe.h"

#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyTroop.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformMisc.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

ALSEnemyCombatProbe::ALSEnemyCombatProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSEnemyCombatProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCombatTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}

void ALSEnemyCombatProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_COMBAT {Result}: {Message}",
		("Function", __FUNCTION__), ("Result", bPassed ? TEXT("PASS") : TEXT("FAIL")), ("Message", Message));
}

void ALSEnemyCombatProbe::Finish()
{
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_COMBAT_FINISHED failures={Failures} maxGuardDistance={Distance}",
		("Function", __FUNCTION__), ("Failures", Failures), ("Distance", MaximumGuardDistance));
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSEnemyCombatProbe::OnExecuted(const FLSAttackExecution& Execution)
{
	if (Execution.Target.Get() == Castle.Get())
	{
		ExecutedSources.Add(Execution.Source);
	}
}

void ALSEnemyCombatProbe::OnWeaponCoreCaptured(const FLSOutpostCaptureSnapshot& Capture)
{
	++WeaponCoreCaptures;
	bWeaponCaptureCleanupObserved &= Core.IsValid() && Capture.Outpost.Get() == Core.Get() &&
		Core->HasBeenCaptured() && !Core->BasicAttack->GetAttackTarget();
	if (Core.IsValid())
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Core.Get());
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			bWeaponCaptureCleanupObserved &= Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
		}
	}
}

void ALSEnemyCombatProbe::OnWeaponOutpostCaptured(const FLSOutpostCaptureSnapshot& Capture)
{
	++WeaponOutpostCaptures;
	bWeaponCaptureCleanupObserved &= Capture.bIsCoreOutpost && Capture.Outpost.Get() == Core.Get();
}

void ALSEnemyCombatProbe::OnWeaponCoreDied(const FLSDamageResult& Damage)
{
	bWeaponLethalSourceObserved = Damage.SourceActor == Castle.Get() && Damage.CurrentHealth == 0;
}

void ALSEnemyCombatProbe::OnPublicCoreDamaged(const FLSDamageResult& Damage)
{
	const AActor* Expected = PublicAttackTroop.IsValid() ? static_cast<AActor*>(PublicAttackTroop.Get()) : Castle.Get();
	bPublicDamageSourceObserved |= Damage.AppliedDamage > 0 && Damage.SourceActor.Get() == Expected;
}

void ALSEnemyCombatProbe::OnCastleDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage > 0 && IsValid(Damage.SourceActor))
	{
		DamageSources.Add(Damage.SourceActor);
		if (Castle.IsValid() && Castle->GetVelocity().Size2D() > 100) { MovingDamageSources.Add(Damage.SourceActor); }
	}
}

void ALSEnemyCombatProbe::OnInvestigationDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage <= 0 || !SupportRanged.IsValid() || Damage.SourceActor.Get() != SupportRanged.Get()) { return; }
	bInvestigationDamagedByGuard = true;
	const FString Reason = SupportRanged->Behavior->GetLastStopReason();
	bInvestigationUsedArrivalScan |= Reason.StartsWith(TEXT("Support arrived:"));
	UE_LOGFMT(LogTemp, Display,
		"[{Function}] ENEMY_INVESTIGATION_DAMAGE source={Source} position={Position} state={State} reason={Reason}",
		("Function", __FUNCTION__), ("Source", SupportRanged->GetName()),
		("Position", SupportRanged->GetActorLocation().ToString()),
		("State", static_cast<int32>(SupportRanged->Behavior->GetState())), ("Reason", Reason));
}

void ALSEnemyCombatProbe::BeginRecoveryCase(bool bShortRoute)
{
	if (RecoveryDecoy.IsValid()) { RecoveryDecoy->Destroy(); }
	for (const auto& Unit : Units)
	{
		if (Unit->Behavior->Role != ELSEnemyRole::Assault) { continue; }
		Unit->Behavior->SetComponentTickEnabled(false);
		Unit->BasicAttack->StopAttack();
		if (AController* Controller = Unit->GetController()) { Controller->StopMovement(); }
		if (Unit->GetTroopType() == (bShortRoute ? ELSTroopType::Archer : ELSTroopType::Spearman)) { RecoveryUnit = Unit; }
	}
	if (!RecoveryUnit.IsValid()) { Check(false, TEXT("recovery case troop available")); Finish(); return; }
	Castle->SetActorLocation(FVector(-2500, -2000, 142));
	RecoveryUnit->SetActorLocation(FVector(-500, bShortRoute ? -2800 : -2500, 88));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RecoveryDecoy = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(
		RecoveryUnit->GetActorLocation() + FVector(bShortRoute ? 900 : 500, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!RecoveryDecoy.IsValid()) { Check(false, TEXT("passive recovery decoy spawned")); Finish(); return; }
	bObservedRecovery = false;
	bChangedRecoveryGoal = false;
	bRecoveryContractHeld = true;
	ProgressBeforeGoalChange = 0;
	MaximumRecoveryProgress = 0;
	RecoveryUnit->Behavior->SetComponentTickEnabled(true);
	Stage = bShortRoute ? 7 : 6;
	StageTime = 0;
}

void ALSEnemyCombatProbe::CaptureCore()
{
	if (RecoveryDecoy.IsValid()) { RecoveryDecoy->Destroy(); }
	if (InvestigationTarget.IsValid()) { InvestigationTarget->Destroy(); }
	if (SupportAttacker.IsValid()) { SupportAttacker->Destroy(); }
	FLSOrder CoreAttack;
	CoreAttack.Kind = ELSOrderKind::AttackTarget;
	CoreAttack.Target = Core.Get();
	Check(Castle->Orders->SubmitOrder(CoreAttack), TEXT("player castle accepts explicit core attack from outside range"));
	Castle->Orders->SubmitOrder(FLSOrder());
	for (const auto& Unit : Units) { Unit->Behavior->SetComponentTickEnabled(true); }
	const auto Result = Core->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(100000, Castle.Get(), Castle.Get()));
	Check(Result.AppliedDamage > 0 && Core->HasBeenCaptured(), TEXT("core capture through direct health damage"));
	Stage = 8; StageTime = 0;
}

void ALSEnemyCombatProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StageTime += DeltaSeconds;
	TotalTime += DeltaSeconds;
	if (TotalTime > 110)
	{
		Check(false, TEXT("bounded scenario timeout"));
		Finish();
		return;
	}
	if (Stage == 0)
	{
		if (StageTime < 1) { return; }
		if ((FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreOrderInspectExit")) ||
			FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreOrderRunExit")) ||
			FParse::Param(FCommandLine::Get(), TEXT("LSEnemyMovingSolo"))) &&
			UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) && StageTime < 8) { return; }
		for (TActorIterator<ALSMobileCastle> It(GetWorld()); It; ++It) { Castle = *It; }
		for (TActorIterator<ALSEnemyOutpost> It(GetWorld()); It; ++It) { Core = *It; }
		for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It) { Units.Add(*It); Starts.Add(*It, It->GetActorLocation()); }
		Check(Castle.IsValid() && Core.IsValid() && Units.Num() == 4, TEXT("explicit scene actors"));
		if (!Castle.IsValid() || !Core.IsValid() || Units.Num() != 4) { Finish(); return; }
		FNavLocation Projected;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		Check(Nav && Nav->ProjectPointToNavigation(Castle->GetActorLocation(), Projected), TEXT("live navigation projection"));
		FNavLocation CoreProjection;
		const bool bCoreProjection = Nav && Nav->ProjectPointToNavigation(Core->GetActorLocation(), CoreProjection, FVector(150, 150, 300));
		UE_LOGFMT(LogTemp, Display,
			"[{Function}] ENEMY_CORE_ORDER_READY control={Control} move={Move} attack={Attack} hostile={Hostile} targetCenterNav={Projected}",
			("Function", __FUNCTION__), ("Control", Castle->Selection->CanPlayerControl()),
			("Move", Castle->Orders->CanMove()), ("Attack", Castle->Orders->CanAttack()),
			("Hostile", ULSCombatTargetLibrary::CanAutoAttackTarget(Castle.Get(), Core.Get())), ("Projected", bCoreProjection));
		if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyMovingSolo")))
		{
			for (const auto& Unit : Units)
			{
				Unit->Behavior->SetComponentTickEnabled(false);
				Unit->BasicAttack->StopAttack();
				if (AController* Controller = Unit->GetController()) { Controller->StopMovement(); }
				if (Unit->Behavior->Role == ELSEnemyRole::Assault && Unit->GetTroopType() == ELSTroopType::Spearman) { MovingSoloUnit = Unit; }
			}
			if (!MovingSoloUnit.IsValid()) { Check(false, TEXT("solo melee unit present")); Finish(); return; }
			Core->Behavior->SetComponentTickEnabled(false);
			Core->BasicAttack->StopAttack();
			Castle->SetActorLocation(FVector(-500, -1700, 142));
			MovingSoloUnit->SetActorLocation(FVector(0, -1700, 88));
			Castle->BasicAttack->StopAttack();
			Castle->FindComponentByClass<ULSHealthComponent>()->OnDamagedEvent.AddUniqueDynamic(this, &ThisClass::OnCastleDamaged);
			Castle->Orders->SetComponentTickEnabled(true);
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = FVector(-3500, -1700, 0);
			Check(Castle->Orders->SubmitOrder(Move), TEXT("solo pursuit castle public movement accepted"));
			MovingSoloUnit->Behavior->SetComponentTickEnabled(true);
			Stage = 92; StageTime = 0;
			return;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreOrderInspectExit")) ||
			FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreOrderRunExit")))
		{
			const bool bRun = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreOrderRunExit"));
			ULSOrderComponent* PublicOrders = Castle->Orders;
			if (bRun)
			{
				// 공개 명령의 접근·피해만 격리한다. 수비 전투는 일반 프로브에서 별도로 확인한다.
				int32 Index = 0;
				for (const auto& Unit : Units)
				{
					Unit->Behavior->SetComponentTickEnabled(false);
					Unit->BasicAttack->StopAttack();
					if (AController* Controller = Unit->GetController()) { Controller->StopMovement(); }
					Unit->SetActorLocation(FVector(-3500, -2800 + Index++ * 200, 88));
				}
				const bool bCorner = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyOrderCorner"));
				if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyOrderMelee")))
				{
					FActorSpawnParameters Spawn;
					Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					PublicAttackTroop = GetWorld()->SpawnActor<ALSTroopUnit>(
						Core->GetActorLocation() + FVector(bCorner ? -1000 : 0, -1000, -12), FRotator::ZeroRotator, Spawn);
					if (!PublicAttackTroop.IsValid()) { Check(false, TEXT("public melee attacker spawn")); Finish(); return; }
					PublicOrders = PublicAttackTroop->Orders;
					Castle->Orders->SetComponentTickEnabled(false);
					Castle->BasicAttack->StopAttack();
					if (AController* Controller = Castle->GetController()) { Controller->StopMovement(); }
				}
				else { Castle->SetActorLocation(Core->GetActorLocation() + FVector(bCorner ? -1000 : 0, -1000, 42)); }
			}
			FLSOrder Order;
			Core->FindComponentByClass<ULSHealthComponent>()->OnDamagedEvent.AddDynamic(this, &ThisClass::OnPublicCoreDamaged);
			if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreWeaponCapture")))
			{
				Core->Behavior->SetComponentTickEnabled(false);
				Core->BasicAttack->StopAttack();
				FLSHealthSnapshot Health;
				Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
				FLSBasicAttackSettings Settings = Castle->BasicAttack->GetAttackSettings();
				// 실제 무기 타격에서 치명 피해부터 함락 통지까지 한 경로로 관찰한다.
				Settings.Damage = Health.CurrentHealth + 1;
				Castle->BasicAttack->ConfigureAttack(Settings);
				Core->OnOutpostCapturedEvent.AddDynamic(this, &ThisClass::OnWeaponOutpostCaptured);
				Core->FindComponentByClass<ULSHealthComponent>()->OnDiedEvent.AddDynamic(this, &ThisClass::OnWeaponCoreDied);
				GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>()->OnCoreOutpostCapturedEvent.AddDynamic(this, &ThisClass::OnWeaponCoreCaptured);
			}
			Order.Kind = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyOrderAttackMove"))
				? ELSOrderKind::AttackMove : ELSOrderKind::AttackTarget;
			Order.Target = Core.Get();
			Order.Destination = Core->GetActorLocation() + FVector(1000, 1000, 0);
			OnBeforePublicAttackOrderEvent.Broadcast(PublicOrders->GetOwner(), Core.Get());
			const bool bAccepted = PublicOrders->SubmitOrder(Order);
			if (const ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance()))
			{
				FNavLocation Side;
				Nav->ProjectPointToNavigation(Core->GetActorLocation() + FVector(225, 0, 0), Side, FVector(100, 100, 300));
				UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_PUBLIC_NAV cell={Cell} side={Side}",
					("Function", __FUNCTION__), ("Cell", Mesh->GetCellSize(ENavigationDataResolution::Default)),
					("Side", FVector::Dist2D(Side.Location, Core->GetActorLocation())));
			}
			Check(bAccepted, TEXT("public combat order accepted from outside core range"));
			if (bAccepted && bRun)
			{
				FLSHealthSnapshot Snapshot;
				Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot);
				CoreHealthBeforePublicAttack = Snapshot.CurrentHealth;
				Stage = 90;
				StageTime = 0;
			}
			else { PublicOrders->SubmitOrder(FLSOrder()); Finish(); }
			return;
		}
		Castle->Orders->SetComponentTickEnabled(false);
		Castle->BasicAttack->StopAttack();
		Castle->FindComponentByClass<ULSHealthComponent>()->OnDamagedEvent.AddDynamic(this, &ALSEnemyCombatProbe::OnCastleDamaged);
		for (const auto& Unit : Units) { Unit->BasicAttack->OnAttackExecutedEvent.AddDynamic(this, &ALSEnemyCombatProbe::OnExecuted); }
		// 실제 이동을 멈추지 않고 전략 목표를 바꾸어 재경로를 관찰한다.
		Castle->SetActorLocation(FVector(-1500, -1700, 142));
		Stage = 1; StageTime = 0;
		return;
	}
	if (!Castle.IsValid() || !Core.IsValid()) { Check(false, TEXT("scene actor unexpectedly removed")); Finish(); return; }
	if (Stage == 92)
	{
		if (StageTime < 12) { return; }
		UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_MOVING_SOLO distance={Distance} castleSpeed={Speed} state={State} reason={Reason}",
			("Function", __FUNCTION__), ("Distance", FVector::Dist(MovingSoloUnit->GetActorLocation(), Castle->GetActorLocation())),
			("Speed", Castle->GetVelocity().Size2D()), ("State", static_cast<int32>(MovingSoloUnit->Behavior->GetState())),
			("Reason", MovingSoloUnit->Behavior->GetLastStopReason()));
		Check(Castle->GetVelocity().Size2D() > 100 && MovingDamageSources.Contains(MovingSoloUnit.Get()),
			TEXT("solo faster melee catches and damages continuously moving castle"));
		Castle->Orders->SubmitOrder(FLSOrder());
		Finish(); return;
	}
	if (Stage == 90)
	{
		ULSOrderComponent* PublicOrders = PublicAttackTroop.IsValid() ? PublicAttackTroop->Orders.Get() : Castle->Orders.Get();
		FLSHealthSnapshot Snapshot;
		Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Snapshot);
		if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyCoreWeaponCapture")))
		{
			if (Core->HasBeenCaptured())
			{
				Check(Snapshot.CurrentHealth == 0 && bWeaponLethalSourceObserved, TEXT("public weapon strike causes lethal core damage with actual source"));
				for (const auto& Unit : Units) { Unit->Behavior->SetComponentTickEnabled(true); }
				Stage = 91; StageTime = 0;
			}
			else if (StageTime > 12) { Check(false, TEXT("weapon core capture timeout")); Finish(); }
			return;
		}
		if (Snapshot.CurrentHealth < CoreHealthBeforePublicAttack)
		{
			Check(bPublicDamageSourceObserved, TEXT("core damage comes from the commanded public attacker"));
			Check(true, TEXT("public combat order actually approaches and damages nav-blocking core"));
			if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyOrderAttackMove")))
			{
				Check(PublicOrders->GetOrderKind() == ELSOrderKind::AttackMove, TEXT("automatic engagement preserves original AttackMove order"));
			}
			PublicOrders->SubmitOrder(FLSOrder());
			Finish();
		}
		else if (StageTime > 12)
		{
			Check(false, TEXT("public core attack damage timeout"));
			PublicOrders->SubmitOrder(FLSOrder());
			Finish();
		}
		return;
	}
	if (Stage == 91)
	{
		if (StageTime < 1) { return; }
		Check(WeaponCoreCaptures == 1 && WeaponOutpostCaptures == 1, TEXT("weapon capture emits exactly one world and one outpost event"));
		Check(bWeaponCaptureCleanupObserved, TEXT("weapon capture callbacks observe stopped attack and collision"));
		FLSOutpostCaptureSnapshot Capture;
		Check(GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>()->TryGetCoreCapture(Capture) && Capture.bIsCoreOutpost,
			TEXT("weapon capture persists in battlefield query"));
		for (const auto& Unit : Units)
		{
			Check(Unit->Behavior->GetState() == ELSEnemyState::Ended && Unit->GetVelocity().IsNearlyZero() && !Unit->BasicAttack->GetAttackTarget(),
				TEXT("actual weapon capture ends each enemy movement and new attacks"));
		}
		Finish(); return;
	}
	for (const auto& Unit : Units)
	{
		if (!Unit.IsValid()) { Check(false, TEXT("enemy unexpectedly removed")); Finish(); return; }
		if (Unit->Behavior->Role == ELSEnemyRole::Guard)
		{
			MaximumGuardDistance = FMath::Max(MaximumGuardDistance, static_cast<float>(FVector::Dist2D(Unit->GetActorLocation(), Core->GetActorLocation())));
		}
	}
	if (Stage == 1 && StageTime > 16)
	{
		for (const auto& Unit : Units)
		{
			if (Unit->Behavior->Role != ELSEnemyRole::Assault) { continue; }
			UE_LOGFMT(LogTemp, Display,
				"[{Function}] ENEMY_COMBAT_ASSAULT type={Type} state={State} distance={Distance} range={Range} position={Position} reason={Reason}",
				("Function", __FUNCTION__), ("Type", static_cast<int32>(Unit->GetTroopType())),
				("State", static_cast<int32>(Unit->Behavior->GetState())),
				("Distance", FVector::Distance(Unit->GetActorLocation(), Castle->GetActorLocation())),
				("Range", Unit->BasicAttack->GetAttackSettings().Range), ("Position", Unit->GetActorLocation().ToString()),
				("Reason", Unit->Behavior->GetLastStopReason()));
			Check(FVector::Dist2D(Unit->GetActorLocation(), Starts[Unit]) > 300, TEXT("assault actual navigation displacement"));
			Check(ExecutedSources.Contains(Unit.Get()), TEXT("assault actual attack execution against relocated castle"));
			Check(DamageSources.Contains(Unit.Get()), TEXT("assault actual damage against relocated castle"));
			// 방어 검증 중 공격 병력이 성의 길을 막지 않도록 실행 주체를 정지한다.
			Unit->Behavior->SetComponentTickEnabled(false);
			Unit->BasicAttack->StopAttack();
			if (AController* Controller = Unit->GetController()) { Controller->StopMovement(); }
		}
		ExecutedSources.Reset(); DamageSources.Reset();
		Castle->SetActorLocation(FVector(2200, 1900, 142));
		Stage = 2; StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 12)
	{
		for (const auto& Unit : Units)
		{
			if (Unit->Behavior->Role != ELSEnemyRole::Guard) { continue; }
			Check(ExecutedSources.Contains(Unit.Get()), TEXT("guard actual attack execution"));
			Check(DamageSources.Contains(Unit.Get()), TEXT("guard actual castle damage"));
			if (Unit->GetTroopType() == ELSTroopType::Spearman)
			{
				Check(FVector::Dist2D(Unit->GetActorLocation(), Starts[Unit]) > 100, TEXT("guard melee actual approach"));
			}
		}
		Castle->SetActorLocation(FVector(-2500, -2000, 142));
		for (const auto& Unit : Units)
		{
			if (Unit->Behavior->Role == ELSEnemyRole::Guard && Unit->GetTroopType() == ELSTroopType::Spearman)
			{
				SupportMelee = Unit;
				const FTransform Transform(FRotator::ZeroRotator, Unit->Behavior->GetInitialPosition());
				ReturnBlocker = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!ReturnBlocker.IsValid()) { Check(false, TEXT("return blocker spawn")); Finish(); return; }
				ReturnBlocker->SetInitialFaction(ELSFaction::Enemy);
				ReturnBlocker->SetInitialTroopType(FParse::Param(FCommandLine::Get(), TEXT("LSEnemyReturnAlternate"))
					? ELSTroopType::Spearman : ELSTroopType::Cavalry);
				ReturnBlocker->FinishSpawning(Transform);
				ReturnBlocker->Orders->SetComponentTickEnabled(false);
				ReturnBlocker->BasicAttack->Deactivate();
				ReturnBlocker->GetCharacterMovement()->DisableMovement();
			}
		}
		Stage = 3; StageTime = 0;
	}
	else if (Stage == 3)
	{
		if (!bRemovedReturnBlocker)
		{
			const bool bAlternate = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyReturnAlternate"));
			bObservedReturnBlocked |= SupportMelee.IsValid() && (bAlternate
				? SupportMelee->Behavior->GetState() == ELSEnemyState::GuardIdle &&
				FVector::Dist2D(SupportMelee->GetActorLocation(), SupportMelee->Behavior->GetInitialPosition()) <= SupportMelee->Behavior->ReturnAreaRadius
				: SupportMelee->Behavior->GetState() == ELSEnemyState::ReturnBlocked);
			if ((bObservedReturnBlocked && StageTime > 2) || StageTime > 8)
			{
				const double ReturnDistance = FVector::Dist2D(SupportMelee->GetActorLocation(), SupportMelee->Behavior->GetInitialPosition());
				UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_RETURN_OBSERVE alternate={Alternate} state={State} distance={Distance}",
					("Function", __FUNCTION__), ("Alternate", bAlternate),
					("State", static_cast<int32>(SupportMelee->Behavior->GetState())), ("Distance", ReturnDistance));
				Check(bObservedReturnBlocked && (bAlternate ? ReturnDistance > 84 : ReturnDistance > SupportMelee->Behavior->ReturnAreaRadius),
					bAlternate ? TEXT("occupied initial point returns to an actual alternative inside return area") :
					TEXT("occupied return area produces blocked state without fake arrival"));
				if (ReturnBlocker.IsValid()) { ReturnBlocker->Destroy(); }
				bRemovedReturnBlocker = true;
			}
		}
		if (StageTime <= 12) { return; }
		for (const auto& Unit : Units)
		{
			if (Unit->Behavior->Role != ELSEnemyRole::Guard) { continue; }
			Check(FVector::Dist2D(Unit->GetActorLocation(), Unit->Behavior->GetInitialPosition()) <= Unit->Behavior->ReturnAreaRadius,
				TEXT("guard observed return area arrival"));
			Check(MaximumGuardDistance <= Core->GetDefenseRadius() + Unit->Behavior->BoundaryAllowance,
				TEXT("guard observed center boundary"));
		}
		// 원격 공격 보고의 위치를 기억하는지 확인하기 위해 별도 실제 투사체를 쏜다.
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ReportedAttackPosition = Core->GetActorLocation() + FVector(1550, 0, 0);
		SupportAttacker = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(ReportedAttackPosition, FRotator::ZeroRotator, Spawn);
		if (!SupportAttacker.IsValid()) { Check(false, TEXT("support attacker spawn")); Finish(); return; }
		FLSBasicAttackSettings Shot;
		Shot.Damage = 1;
		Shot.Interval = 10;
		Shot.Range = 1700;
		SupportAttacker->Attack->ConfigureAttack(Shot);
		FLSProjectileAttackSettings Projectile;
		Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
		Projectile.Speed = 4000;
		SupportAttacker->Attack->ConfigureProjectile(Projectile);
		FLSHealthSnapshot CoreHealth;
		Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(CoreHealth);
		CoreHealthBeforeSupport = CoreHealth.CurrentHealth;
		for (const auto& Unit : Units)
		{
			if (Unit->Behavior->Role != ELSEnemyRole::Guard) { continue; }
			if (Unit->GetTroopType() == ELSTroopType::Spearman) { SupportMelee = Unit; SupportMeleeStart = Unit->GetActorLocation(); }
			else { SupportRanged = Unit; SupportRangedStart = Unit->GetActorLocation(); }
		}
		Check(SupportAttacker->Attack->StartAttack(Core.Get()), TEXT("real remote projectile attack accepted"));
		Stage = 4; StageTime = 0;
	}
	else if (Stage == 4)
	{
		FLSHealthSnapshot CoreHealth;
		Core->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(CoreHealth);
		if (CoreHealth.CurrentHealth < CoreHealthBeforeSupport)
		{
			Check(true, TEXT("remote projectile actually damaged core"));
			SupportAttacker->Attack->StopAttack();
			SupportAttacker->SetActorLocation(FVector(3800, 3200, 100));
			Stage = 5; StageTime = 0;
		}
		else if (StageTime > 5) { Check(false, TEXT("remote projectile hit timeout")); Finish(); }
	}
	else if (Stage == 5)
	{
		if (!SupportMelee.IsValid() || !SupportRanged.IsValid()) { Check(false, TEXT("support defenders alive")); Finish(); return; }
		if (!InvestigationTarget.IsValid())
		{
			FLSUnitStatRow Stats;
			SupportRanged->TryGetStats(Stats);
			const float Range = SupportRanged->BasicAttack->GetAttackSettings().Range;
			const float SensorGap = Stats.DetectionRange - Range;
			const FVector Here = SupportRanged->GetActorLocation();
			const double ReportDistance = FVector::Dist2D(Here, ReportedAttackPosition);
			if (SensorGap > 0 && ReportDistance <= Range + SensorGap * 0.5f && ReportDistance > Range + 5)
			{
				const FVector AwayFromHome = (Here - SupportRanged->Behavior->GetInitialPosition()).GetSafeNormal2D();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				InvestigationTarget = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(
					Here + AwayFromHome * (Range + SensorGap * 0.75f), FRotator::ZeroRotator, Spawn);
				if (!InvestigationTarget.IsValid()) { Check(false, TEXT("passive investigation target spawned")); Finish(); return; }
				InvestigationHealth = InvestigationTarget->GetCurrentHealth();
				InvestigationTarget->HealthComponent->OnDamagedEvent.AddDynamic(this, &ThisClass::OnInvestigationDamaged);
				const double Distance = FVector::Distance(Here, InvestigationTarget->GetActorLocation());
				Check(Distance > Range && Distance < Stats.DetectionRange,
					TEXT("unreported target spawned outside firing range and inside actual detection"));
			}
		}
		SupportMeleeMovement = FMath::Max(SupportMeleeMovement, static_cast<float>(FVector::Dist2D(SupportMelee->GetActorLocation(), SupportMeleeStart)));
		SupportRangedMovement = FMath::Max(SupportRangedMovement, static_cast<float>(FVector::Dist2D(SupportRanged->GetActorLocation(), SupportRangedStart)));
		ClosestReportedPosition = FMath::Min(ClosestReportedPosition, static_cast<float>(FVector::Dist2D(SupportRanged->GetActorLocation(), ReportedAttackPosition)));
		bObservedSupporting |= SupportRanged->Behavior->GetState() == ELSEnemyState::Supporting;
		if (StageTime <= 10) { return; }
		Check(SupportMeleeMovement < 50, TEXT("melee defender does not chase unreachable outside attacker"));
		Check(bObservedSupporting && SupportRangedMovement > 300, TEXT("ranged defender reacts to own outpost support"));
		Check(ClosestReportedPosition <= SupportRanged->BasicAttack->GetAttackSettings().Range + 50,
			TEXT("support investigates reported position after attacker moved out of detection"));
		Check(InvestigationTarget.IsValid() && InvestigationTarget->GetCurrentHealth() < InvestigationHealth &&
			bInvestigationDamagedByGuard && bInvestigationUsedArrivalScan,
			TEXT("support arrival local rescan causes actual damage to unreported target"));
		if (InvestigationTarget.IsValid()) { InvestigationTarget->Destroy(); }
		BeginRecoveryCase(false);
	}
	else if (Stage == 6 || Stage == 7)
	{
		const bool bShortRoute = Stage == 7;
		if (!RecoveryUnit.IsValid() || !RecoveryDecoy.IsValid())
		{
			Check(false, TEXT("recovery actors remain alive")); Finish(); return;
		}
		ULSEnemyBehaviorComponent* Behavior = RecoveryUnit->Behavior;
		const bool bRecovering = Behavior->GetState() == ELSEnemyState::MarchRecovery;
		if (!bObservedRecovery)
		{
			if (bRecovering)
			{
				bObservedRecovery = true;
				RecoveryCenter = Behavior->GetEngagementCenter();
				Check(true, bShortRoute ? TEXT("short-route chase reached MarchRecovery") : TEXT("moving-goal chase reached MarchRecovery"));
			}
			else
			{
				// 타격 거리 밖의 표적을 이동시켜 실제 추적 한계를 넘긴다.
				RecoveryDecoy->SetActorLocation(RecoveryUnit->GetActorLocation() + FVector(bShortRoute ? 900 : 500, 0, 0));
			}
		}
		if (bObservedRecovery)
		{
			const float Progress = Behavior->GetRecoveryProgress();
			MaximumRecoveryProgress = FMath::Max(MaximumRecoveryProgress, Progress);
			if (bRecovering)
			{
				bRecoveryContractHeld &= Behavior->GetTarget() != RecoveryDecoy.Get() &&
					Behavior->GetEngagementCenter().Equals(RecoveryCenter, 1);
			}
			if (!bChangedRecoveryGoal && (bShortRoute || Progress > 20))
			{
				ProgressBeforeGoalChange = Progress;
				Castle->SetActorLocation(bShortRoute ? RecoveryUnit->GetActorLocation() + FVector(-900, 0, 54) : FVector(-2200, -1200, 142));
				bChangedRecoveryGoal = true;
			}
			else if (bChangedRecoveryGoal)
			{
				bRecoveryContractHeld &= Progress + 1 >= ProgressBeforeGoalChange;
			}
			if (!bRecovering && bChangedRecoveryGoal)
			{
				Check(bRecoveryContractHeld, bShortRoute ? TEXT("short recovery keeps center and rejects decoy") : TEXT("moving-goal recovery preserves progress center and rejects decoy"));
				if (bShortRoute)
				{
					Check(FVector::Dist(RecoveryUnit->GetActorLocation(), Castle->GetActorLocation()) <= RecoveryUnit->BasicAttack->GetAttackSettings().Range &&
						MaximumRecoveryProgress < Behavior->RecoveryAdvanceDistance,
						TEXT("short recovery exits at actual castle attack range before full advance distance"));
					CaptureCore();
				}
				else
				{
					Check(MaximumRecoveryProgress >= Behavior->RecoveryAdvanceDistance,
						TEXT("moving-goal recovery exits after measured full advance distance"));
					BeginRecoveryCase(true);
				}
				return;
			}
		}
		if (StageTime > 15)
		{
			Check(false, bShortRoute ? TEXT("short-route recovery observation timeout") : TEXT("moving-goal recovery observation timeout"));
			Finish();
		}
	}
	else if (Stage == 8 && StageTime > 1)
	{
		for (const auto& Unit : Units)
		{
			Check(Unit->Behavior->GetState() == ELSEnemyState::Ended, TEXT("enemy entered end-await after core capture"));
			Check(Unit->GetVelocity().Size() < 1 && !Unit->BasicAttack->GetAttackTarget(), TEXT("ended enemy movement and attack stopped"));
		}
		Finish();
	}
}
