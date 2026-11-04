#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/LSCastle.h"
#include "Castle/LSCastleAIController.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSSelectableComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSOutpost.h"
#include "Match/LSGameState.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Maps/LSBattlefieldLayoutAuthoring.h"
#include "Maps/LSBattlefieldCastleNavMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "Navigation/LSMineAvoidance.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshPath.h"
#include "NavAreas/NavArea_Null.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSPathObstacle.h"
#include "Obstacles/LSTurret.h"
#include "Tests/AutomationEditorCommon.h"
#include "Troops/LSTroopUnit.h"
#include "UObject/UnrealType.h"
#include "Vision/LSVisionSubsystem.h"

namespace
{
constexpr float CastleNavigationRadius = 580.0f;
constexpr double GateX = 6500;
constexpr double GateFarFace = GateX + 70;
constexpr double GateHalfWidth = 600;
const FVector CastleStage(5550, 0, 500);
const FVector CastleGoal(GateFarFace + 1200, 0, 0);

struct FLSBreachObservation
{
	int32 HP = 600;
	int32 Crew = 6;
	int32 TrialIndex = 1;
	FString MovePathJson = TEXT("[]");
	FName GateName;
	int64 GateReceiverId = 0;
	double FirstDamage = -1;
	double DeathDamage = -1;
	double CollisionOff = -1;
	double NavigationReady = -1;
	double MoveStart = -1;
	double FullClear = -1;
	bool bCollisionDisabledAtDeath = false;
	bool bSawYield = false;
	bool bLoggedOneSecond = false;
	bool bLoggedThreeSeconds = false;
	bool bLoggedEarlyIdle = false;
};

struct FLSBreachCalibrationState
{
	FAutomationTestBase* Test = nullptr;
	FString OriginalFilename, SourceFilename, StopReason;
	FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FString ProgressReportFilename, CompletedReportFilename;
	FMD5Hash SourceHash;
	TWeakObjectPtr<UWorld> EditorCopy;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ALSCastle> Castle;
	TWeakObjectPtr<ALSOutpost> Core;
	TWeakObjectPtr<ALSBarricade> Gate;
	TArray<TWeakObjectPtr<ALSTroopUnit>> Workers;
	TSharedPtr<FLSBreachObservation> Current;
	TArray<FString> Results;
	FDelegateHandle DestroyedHandle;
	int32 HP = 600;
	int32 Crew = 6;
	int32 ChosenHP = 0;
	int32 TrialIndex = 1;
	bool bFinished = false;
	bool bCompletedReportWritten = false;
	bool bReady = false;
};

bool HasCompleteCastlePath(UWorld* World, ALSCastle* Castle)
{
	if (Castle->GetWorld() != World) return false;
	const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(Castle, CastleGoal, {});
	if (Safe.Result != ELSMinePathResult::SafePath || !Safe.Path.IsValid() || !Safe.Path->IsValid() || Safe.Path->IsPartial() || Safe.Path->GetPathPoints().Num() < 2) return false;
	const double CenterClearance = GateHalfWidth - Castle->GetCapsuleComponent()->GetScaledCapsuleRadius();
	for (const FNavPathPoint& Point : Safe.Path->GetPathPoints())
		if (FMath::Abs(Point.Location.Y) > CenterClearance) return false;
	return true;
}

void ReportOpenCastlePathProbe(FAutomationTestBase& Test, UWorld* World, ALSCastle* Castle, bool bProfilesReady)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation()) : nullptr;
	FNavLocation From, To;
	const bool bFrom = Data && Nav->ProjectPointToNavigation(Castle->GetNavAgentLocation(), From, FVector(150, 150, 300), Data);
	const bool bTo = Data && Nav->ProjectPointToNavigation(CastleGoal, To, FVector(150, 150, 300), Data);
	FVector Hit = FVector::ZeroVector;
	const bool bRayBlocked = bFrom && bTo && Data->Raycast(From.Location, To.Location, Hit, Data->GetDefaultQueryFilter(), Castle);
	FPathFindingResult Independent;
	if (bFrom && bTo)
	{
		FPathFindingQuery Query(Castle, *Data, From.Location, To.Location);
		Query.SetAllowPartialPaths(false);
		Independent = Nav->FindPathSync(Castle->GetNavAgentPropertiesRef(), Query);
	}
	Test.AddInfo(FString::Printf(TEXT("CrowdedOpenPathProbe profilesReady=%d data=%s class=%s pawnProps=%.3f/%.3f registered=%.3f/%.3f dirty=%d remaining=%d actor=%s navOrigin=%s goal=%s projectFrom=%d:%s projectTo=%d:%s directRayBlocked=%d rayHit=%s independentResult=%d success=%d valid=%d partial=%d points=%d; raycast is diagnostic only, preflight requires production complete path inside actual opening strip"),
		bProfilesReady, *GetNameSafe(Data), Data ? *Data->GetClass()->GetPathName() : TEXT("none"), Castle->GetNavAgentPropertiesRef().AgentRadius, Castle->GetNavAgentPropertiesRef().AgentHeight,
		Data ? Data->GetConfig().AgentRadius : -1, Data ? Data->GetConfig().AgentHeight : -1, Nav && Nav->HasDirtyAreasQueued(), Nav ? Nav->GetNumRemainingBuildTasks() : -1,
		*Castle->GetActorLocation().ToString(), *Castle->GetNavAgentLocation().ToString(), *CastleGoal.ToString(), bFrom, bFrom ? *From.Location.ToString() : TEXT("none"), bTo, bTo ? *To.Location.ToString() : TEXT("none"),
		bRayBlocked, *Hit.ToString(), static_cast<int32>(Independent.Result), Independent.IsSuccessful(), Independent.Path.IsValid() && Independent.Path->IsValid(),
		Independent.Path.IsValid() && Independent.Path->IsPartial(), Independent.Path.IsValid() ? Independent.Path->GetPathPoints().Num() : 0));
	if (Independent.Path.IsValid())
		for (int32 Index = 0; Index < Independent.Path->GetPathPoints().Num(); ++Index)
			Test.AddInfo(FString::Printf(TEXT("CrowdedOpenPathProbe point=%d nav=%s"), Index, *Independent.Path->GetPathPoints()[Index].Location.ToString()));
}

bool ProfilesReady(UWorld* World)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) || Nav->GetSupportedAgents().Num() != 2) return false;
	for (const FNavDataConfig& Profile : Nav->GetSupportedAgents())
	{
		const ANavigationData* Data = Nav->GetNavDataForProps(Profile);
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
		if (!Recast) return false;
		TArray<FNavTileRef> Tiles;
		Recast->GetAllNavMeshTiles(Tiles);
		bool bPopulated = false;
		for (const FNavTileRef Tile : Tiles)
			if (Recast->GetNavMeshTileBounds(Tile).IsValid) { bPopulated = true; break; }
		FNavLocation Stage, Goal;
		if (!bPopulated || !Nav->ProjectPointToNavigation(FVector(CastleStage.X, 0, 0), Stage, FVector(150, 150, 300), Data) ||
			!Nav->ProjectPointToNavigation(CastleGoal, Goal, FVector(150, 150, 300), Data)) return false;
	}
	return true;
}

FString ObservationJson(const FLSBreachObservation& Value)
{
	return FString::Printf(TEXT("{\"gate_hp\":%d,\"spearmen\":%d,\"trial_index\":%d,\"castle_move_path_cm\":%s,\"first_positive_ally_damage_seconds\":%.9f,\"lethal_damage_seconds\":%.9f,\"death_collision_off_seconds\":%.9f,\"collision_disabled_at_death\":%s,\"navigation_ready_seconds\":%.9f,\"public_move_start_seconds\":%.9f,\"entire_sphere_clears_seconds\":%.9f,\"first_damage_to_full_clear_seconds\":%.9f,\"observed_worker_yield\":%s}"),
		Value.HP, Value.Crew, Value.TrialIndex, *Value.MovePathJson, Value.FirstDamage, Value.DeathDamage, Value.CollisionOff, Value.bCollisionDisabledAtDeath ? TEXT("true") : TEXT("false"),
		Value.NavigationReady, Value.MoveStart, Value.FullClear, Value.FullClear >= 0 ? Value.FullClear - Value.FirstDamage : -1,
		Value.bSawYield ? TEXT("true") : TEXT("false"));
}

void WriteCalibrationReport(const TSharedRef<FLSBreachCalibrationState>& State)
{
	const FString Reason = State->StopReason.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT("\\r"));
	FString Json = FString::Printf(TEXT("{\n\"test\":\"Landship.Maps.Battlefield.ActualCastleBreachCalibration\",\n\"scope\":\"Isolated actual-map shoulder geometry. Original troops, path obstacles, non-Core hostile outposts and encounter coordinator removed from an unsaved template. Configured Core and battlefield setup remain for native castle ownership and normal battle lifecycle. Native allied workers remain through each measured crossing. This is not a full-army battle.\",\n\"chosen_max_hp\":%d,\n\"calibration_complete\":%s,\n\"stop_reason\":\"%s\",\n\"castle_radius_cm\":500,\n\"full_clear_center_x_cm\":%.1f,\n\"window_min_seconds\":6,\n\"window_max_seconds\":10,\n\"trials\":[\n%s\n]"),
		State->ChosenHP, State->ChosenHP > 0 ? TEXT("true") : TEXT("false"), *Reason, GateFarFace + 500, *FString::Join(State->Results, TEXT(",\n")));
	const FString Progress = State->ProgressReportFilename.Replace(TEXT("\\"), TEXT("/"));
	const FString Completed = State->CompletedReportFilename.Replace(TEXT("\\"), TEXT("/"));
	Json += FString::Printf(TEXT(",\n\"run_id\":\"%s\",\n\"run_finished\":%s,\n\"progress_report_path\":\"%s\",\n\"completed_report_path\":\"%s\",\n\"source_map\":\"/Game/Landship/Maps/L_BattlefieldPrototype\",\n\"required_trials_per_crew_at_selected_hp\":3,\n\"castle_stage_cm\":[5550,0,500],\n\"castle_goal_cm\":[7770,0,0],\n\"gate_box_half_extent_cm\":[70,600,90]"),
		*State->RunId, State->bFinished ? TEXT("true") : TEXT("false"), *Progress, *Completed);
	Json += FString::Printf(TEXT(",\n\"castle_navigation_radius_cm\":%.1f,\n\"castle_navigation_height_cm\":1000"), CastleNavigationRadius);
	if (State->Current && State->Current->FullClear < 0)
		Json += TEXT(",\n\"incomplete_trial\":") + ObservationJson(*State->Current);
	Json += TEXT("\n}\n");
	State->Test->TestTrue(TEXT("Write latest breach calibration progress"), FFileHelper::SaveStringToFile(Json, *State->ProgressReportFilename));
	if (State->bFinished && !State->bCompletedReportWritten)
	{
		State->bCompletedReportWritten = FFileHelper::SaveStringToFile(Json, *State->CompletedReportFilename,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_NoReplaceExisting);
		State->Test->TestTrue(TEXT("Create immutable run-specific completed calibration report"), State->bCompletedReportWritten);
	}
}

class FLSBreachCalibrationCommand : public IAutomationLatentCommand
{
public:
	explicit FLSBreachCalibrationCommand(TSharedRef<FLSBreachCalibrationState> InState) : State(InState) {}
	virtual bool Update() override
	{
		if (!State->bReady) return true;
		FAutomationTestBase& Test = *State->Test;
		if (Phase == 90)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
			{
				if (Elapsed() < 30) return false;
				Test.AddError(TEXT("Calibration PIE did not end"));
			}
			return true;
		}
		if (Phase == 0)
		{
			UWorld* EditorWorld = State->EditorCopy.Get();
			if (!ProfilesReady(EditorWorld)) return Wait(TEXT("Both editor navigation profiles must finish before PIE"), 60);
			FRequestPlaySessionParams Params;
			Params.bAllowOnlineSubsystem = false;
			Starter = MakeUnique<FStartPIEForAutomationCommand>(Params);
			Advance(1);
		}
		if (Phase == 1)
		{
			if (!Starter->Update()) return false;
			Starter.Reset();
			State->World = GEditor->PlayWorld;
			if (!State->World.IsValid() || !State->World->HasBegunPlay()) return Fail(TEXT("Actual PIE world did not begin play"));
			for (TActorIterator<ALSCastle> It(State->World.Get()); It; ++It) State->Castle = *It;
			if (!State->Castle.IsValid()) return Fail(TEXT("Native actual castle absent in copied PIE world"));
			int32 SetupCount = 0;
			for (TActorIterator<ALSBattlefieldSetup> It(State->World.Get()); It; ++It) { ++SetupCount; State->Core = It->CoreOutpost.Get(); }
			if (SetupCount != 1 || !State->Core.IsValid()) return Fail(TEXT("Copied PIE world requires its own unique battlefield setup and retained Core"));
			if (!CheckFixtureInvariants(TEXT("PIE setup"))) return Finish();
			FLSCastleStatRow Stats;
			if (!State->Castle->TryGetStats(Stats) || Stats.MovementSpeed != 400) return Fail(TEXT("Actual castle stats must provide speed 400"));
			Test.TestTrue(TEXT("Calibration uses native APawn castle"), State->Castle->GetClass() == ALSCastle::StaticClass());
			Test.TestEqual(TEXT("Actual capsule radius"), State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius(), 500.0f);
			Test.TestEqual(TEXT("Actual capsule half-height"), State->Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 500.0f);
			Test.TestEqual(TEXT("Original castle health row"), Stats.Health, 1000.0f);
			Test.TestEqual(TEXT("Original castle sight row"), Stats.VisionRadius, 1600.0f);
			for (TActorIterator<AActor> It(State->World.Get()); It; ++It)
			{
				if (ULSBasicAttackComponent* Attack = It->FindComponentByClass<ULSBasicAttackComponent>()) Attack->EndBattleActions();
				if (ULSEnemyBehaviorComponent* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>()) Behavior->EndBattleActions();
			}
			State->DestroyedHandle = State->World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda([Shared = State](AActor* Actor)
			{
				if (!Shared->Current || Actor->GetFName() != Shared->Current->GateName) return;
				Shared->Current->CollisionOff = Shared->World->GetTimeSeconds();
				Shared->Current->bCollisionDisabledAtDeath = true;
				TInlineComponentArray<UPrimitiveComponent*> Bodies(Actor);
				for (const UPrimitiveComponent* Body : Bodies)
					Shared->Current->bCollisionDisabledAtDeath &= Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
			}));
			Advance(2);
		}
		if (!State->World.IsValid()) return Fail(TEXT("Calibration PIE ended unexpectedly"));
		if (Phase >= 2 && Phase <= 7 && !CheckFixtureInvariants(TEXT("active calibration trial"))) return Finish();
		if (Phase == 2)
		{
			if (!ProfilesReady(State->World.Get()) || !HasCompleteCastlePath(State->World.Get(), State->Castle.Get())) return Wait(TEXT("Reset fixture must have complete open castle corridor"), 30);
			if (!BeginTrial()) return Finish();
			Advance(3);
			return false;
		}
		if (Phase == 3)
		{
			if (!ProfilesReady(State->World.Get())) return Wait(TEXT("Fresh gate navigation must finish"), 30);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
			const ANavigationData* Data = Nav->GetNavDataForProps(State->Castle->GetNavAgentPropertiesRef());
			FVector Hit;
			if (!Data->Raycast(FVector(CastleStage.X, 0, 10), CastleGoal + FVector(0, 0, 10), Hit, Data->GetDefaultQueryFilter(), State->Castle.Get()))
				return Wait(TEXT("Intact 12m gate must block direct castle navigation"), 30);
			State->World->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
			for (const TWeakObjectPtr<ALSTroopUnit>& Worker : State->Workers)
			{
				FLSOrder Attack;
				Attack.Kind = ELSOrderKind::AttackTarget;
				Attack.Target = State->Gate;
				Attack.Destination = FVector(GateX, 0, 0);
				if (!Worker->Orders->SubmitOrder(Attack)) return Fail(TEXT("Native spearman public AttackTarget rejected; do not tune HP around path failure"));
			}
			Advance(4);
			return false;
		}
		if (Phase == 4)
		{
			if (State->Current->CollisionOff < 0) return Wait(TEXT("Actual spearmen must destroy gate through normal damage"), 30);
			Advance(6);
			return false;
		}
		if (Phase == 6)
		{
			if (State->Current->FirstDamage < 0 || State->Current->DeathDamage < 0) return Wait(TEXT("Deferred native committed-damage records must arrive"), 5);
			if (!Test.TestTrue(TEXT("Gate death disables all collision before actor destruction"), State->Current->bCollisionDisabledAtDeath)) return Finish();
			Advance(7);
			return false;
		}
		if (Phase == 7)
		{
			if (!ProfilesReady(State->World.Get()) || !HasCompleteCastlePath(State->World.Get(), State->Castle.Get())) return Wait(TEXT("Destroyed gate must restore complete path through actual opening"), 30);
			State->Current->NavigationReady = State->World->GetTimeSeconds();
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = CastleGoal;
			if (!State->Castle->Orders->SubmitOrder(Move)) return Fail(TEXT("Actual castle public move rejected after complete path; do not tune HP"));
			State->Current->MoveStart = State->World->GetTimeSeconds();
			AAIController* AI = Cast<AAIController>(State->Castle->GetController());
			const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
			const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
			TArray<FString> Points;
			if (Path.IsValid())
				for (const FNavPathPoint& Point : Path->GetPathPoints())
					Points.Add(FString::Printf(TEXT("[%.6f,%.6f,%.6f]"), Point.Location.X, Point.Location.Y, Point.Location.Z));
			State->Current->MovePathJson = TEXT("[") + FString::Join(Points, TEXT(",")) + TEXT("]");
			LogMovementSnapshot(TEXT("public Move accepted"));
			Advance(5);
			return false;
		}
		if (Phase == 5)
		{
			const double SinceMove = State->World->GetTimeSeconds() - State->Current->MoveStart;
			if (!State->Current->bLoggedOneSecond && SinceMove >= 1)
			{
				State->Current->bLoggedOneSecond = true;
				LogMovementSnapshot(TEXT("one second after Move"));
				LogDebugBodySweeps(TEXT("one second after Move"));
			}
			if (!State->Current->bLoggedThreeSeconds && SinceMove >= 3 &&
				State->Castle->GetActorLocation().X - State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() < GateFarFace)
			{
				State->Current->bLoggedThreeSeconds = true;
				LogDebugBodySweeps(TEXT("three seconds while crossing incomplete"));
			}
			if (!State->Current->bLoggedEarlyIdle && State->Castle->Orders->GetOrderKind() == ELSOrderKind::Idle && State->Castle->GetActorLocation().X < GateFarFace + 500)
			{
				State->Current->bLoggedEarlyIdle = true;
				LogMovementSnapshot(TEXT("Move ended before full sphere clearance"));
			}
			for (const TWeakObjectPtr<ALSTroopUnit>& Worker : State->Workers)
			{
				if (!Worker.IsValid() || !ULSCombatTargetLibrary::IsCombatAlive(Worker.Get())) return Fail(TEXT("A worker died or disappeared before castle clearance"));
				State->Current->bSawYield |= Worker->Orders->IsYieldingToCastle();
			}
			if (State->Castle->GetActorLocation().X - State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() < GateFarFace)
				return Wait(TEXT("Entire actual 500cm sphere must clear far gate face with workers retained"), 20);
			if (FMath::Abs(State->Castle->GetActorLocation().Y) > GateHalfWidth - State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius())
				return Fail(TEXT("Actual castle full-clear center must remain inside the real gate opening strip"));
			State->Current->FullClear = State->World->GetTimeSeconds();
			RecordTrial();
			const double Duration = State->Current->FullClear - State->Current->FirstDamage;
			if (Duration < 6 || Duration > 10)
			{
				Test.AddInfo(FString::Printf(TEXT("Reject HP=%d after completed crew=%d trial=%d took %.6fs; skip remaining trials at this HP."), State->HP, State->Crew, State->TrialIndex, Duration));
				State->Crew = 6;
				State->TrialIndex = 1;
				State->HP -= 50;
				if (State->HP < 50) return Fail(TEXT("No gate HP in 600..50 by 50 satisfies all three observed trials for both crews"));
			}
			else if (State->TrialIndex < 3) ++State->TrialIndex;
			else if (State->Crew == 6) { State->Crew = 8; State->TrialIndex = 1; }
			else
			{
				State->ChosenHP = State->HP;
				Test.AddInfo(FString::Printf(TEXT("Maximum calibrated gate HP=%d; all three actual trials for each 6/8-spearman crew fall within 6-10 seconds."), State->HP));
				return Finish();
			}
			ResetTrial();
			Advance(2);
			return false;
		}
		return false;
	}
private:
	bool CheckFixtureInvariants(const TCHAR* Stage)
	{
		const auto Reject = [this, Stage](const FString& Reason)
		{
			State->StopReason = Reason;
			State->Test->AddError(FString::Printf(TEXT("Calibration fixture isolation failure at %s: %s"), Stage, *Reason));
			return false;
		};
		UWorld* World = State->World.Get();
		if (!World || !State->Castle.IsValid() || !State->Core.IsValid()) return Reject(TEXT("PIE castle/Core references are missing"));
		const ULSBattlefieldStateSubsystem* Battlefield = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
		FLSPlayerCastleState Ownership;
		if (!Battlefield || !Battlefield->IsSetupValid() || !Battlefield->TryGetPlayerCastleState(Ownership) ||
			Ownership.Castle.Get() != State->Castle.Get() || Battlefield->GetCoreOutpost() != State->Core.Get())
			return Reject(TEXT("Native battlefield setup must retain actual PIE castle/Core ownership"));
		const ALSGameState* GameState = World->GetGameState<ALSGameState>();
		if (!GameState || !GameState->IsBattleOutcomeEnabled() || GameState->HasBattleEnded() || !ULSCombatTargetLibrary::IsCombatAlive(State->Core.Get()))
			return Reject(TEXT("Retained Core and normal GameState lifecycle must remain alive/in progress"));
		int32 Outposts = 0;
		for (TActorIterator<ALSOutpost> It(World); It; ++It)
		{
			++Outposts;
			if (*It != State->Core.Get()) return Reject(TEXT("Unexpected non-Core outpost retained: ") + It->GetPathName());
		}
		if (Outposts != 1) return Reject(TEXT("Exactly the configured Core must remain as an original outpost"));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(*It);
			if (Faction && Faction->GetFaction() == ELSFaction::Enemy && ULSCombatTargetLibrary::IsCombatAlive(*It) &&
				*It != State->Core.Get() && *It != State->Gate.Get())
				return Reject(TEXT("Unexpected live hostile target in isolated fixture: ") + It->GetPathName());
		}
		for (const TWeakObjectPtr<ALSTroopUnit>& WeakWorker : State->Workers)
		{
			const ALSTroopUnit* Worker = WeakWorker.Get();
			if (!Worker) continue;
			FLSUnitStatRow Stats;
			if (!Worker->TryGetStats(Stats)) return Reject(TEXT("Native calibration worker stats are not initialized"));
			const double CoreDistance = ULSCombatTargetLibrary::GetAttackDistance(Worker, State->Core.Get());
			if (CoreDistance <= FMath::Max(Stats.DetectionRange, Worker->BasicAttack->GetAttackSettings().Range))
				return Reject(FString::Printf(TEXT("Preserved Core overlaps native worker acquisition/attack region: worker=%s distance=%.3f detection=%.3f"), *Worker->GetName(), CoreDistance, Stats.DetectionRange));
			for (AActor* Target : {Worker->Orders->GetEngagedTarget(), Worker->BasicAttack->GetAttackTarget()})
				if (ULSCombatTargetLibrary::IsCombatAlive(Target) && Target != State->Gate.Get())
					return Reject(FString::Printf(TEXT("Worker acquired unrelated live hostile target: worker=%s target=%s"), *Worker->GetName(), *Target->GetPathName()));
		}
		return true;
	}
	bool BeginTrial()
	{
		UWorld* World = State->World.Get();
		State->Current = MakeShared<FLSBreachObservation>();
		State->Current->HP = State->HP;
		State->Current->Crew = State->Crew;
		State->Current->TrialIndex = State->TrialIndex;
		const FTransform Transform(FRotator::ZeroRotator, FVector(GateX, 0, 90));
		ALSBarricade* Gate = World->SpawnActorDeferred<ALSBarricade>(ALSBarricade::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Gate) { State->Test->AddError(TEXT("Fresh calibration gate spawn failed")); return false; }
		FNumericProperty* HPProperty = FindFProperty<FNumericProperty>(ALSPathObstacle::StaticClass(), TEXT("MaxHealth"));
		HPProperty->SetFloatingPointPropertyValue(HPProperty->ContainerPtrToValuePtr<void>(Gate), State->HP);
		CastChecked<UBoxComponent>(Gate->GetObstacleBody())->SetBoxExtent(FVector(70, GateHalfWidth, 90), false);
		const TSharedRef<FLSBreachObservation> Observation = State->Current.ToSharedRef();
		Observation->GateReceiverId = Gate->GetHealthComponent()->GetDamageReceiverId();
		Gate->GetHealthComponent()->OnDamageCommitted.AddWeakLambda(World, [Observation](const FLSDamageResult& Damage)
		{
			if (Damage.ReceiverId != Observation->GateReceiverId || Damage.AppliedDamage <= 0 || !Damage.bHasSourceFaction || Damage.SourceFaction != ELSFaction::Ally) return;
			if (Observation->FirstDamage < 0) Observation->FirstDamage = Damage.TimestampSeconds;
			if (Damage.CurrentHealth <= 0) Observation->DeathDamage = Damage.TimestampSeconds;
		});
		Gate->FinishSpawning(Transform);
		State->Gate = Gate;
		State->Current->GateName = Gate->GetFName();
		for (int32 Index = 0; Index < State->Crew; ++Index)
		{
			const float Y = (Index - (State->Crew - 1) * 0.5f) * 200;
			const FTransform TroopTransform(FRotator::ZeroRotator, FVector(6138, Y, 90));
			ALSTroopUnit* Worker = World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), TroopTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Worker) { State->Test->AddError(TEXT("Native calibration spearman spawn failed")); return false; }
			Worker->SetInitialFaction(ELSFaction::Ally);
			Worker->SetInitialTroopType(ELSTroopType::Spearman);
			Worker->FinishSpawning(TroopTransform);
			State->Workers.Add(Worker);
		}
		return true;
	}
	void LogMovementSnapshot(const TCHAR* Stage)
	{
		if (!State->Castle.IsValid()) return;
		ALSCastle* Castle = State->Castle.Get();
		AAIController* AI = Cast<AAIController>(Castle->GetController());
		const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
		const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
		const ULSCastleMovementComponent* Movement = Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent());
		State->Test->AddInfo(FString::Printf(TEXT("BreachMotion %s HP=%d crew=%d castle=%s velocity=%s order=%d failure=%d success=%d moving=%d AIstatus=%d pathValid=%d pathPartial=%d next=%d endpoint=%s maxSpeed=%.1f"), Stage,
			State->HP, State->Crew, *Castle->GetActorLocation().ToString(), *Castle->GetVelocity().ToString(), static_cast<int32>(Castle->Orders->GetOrderKind()),
			static_cast<int32>(Castle->Orders->GetLastFailure()), Castle->Orders->DidLastMovementSucceed(), Castle->IsMoving(), AI ? static_cast<int32>(AI->GetMoveStatus()) : -1,
			Path.IsValid() && Path->IsValid(), Path.IsValid() && Path->IsPartial(), Following ? static_cast<int32>(Following->GetNextPathIndex()) : -1,
			Path.IsValid() && !Path->GetPathPoints().IsEmpty() ? *Path->GetPathPoints().Last().Location.ToString() : TEXT("none"), Movement ? Movement->MaxSpeed : -1));
		for (int32 Index = 0; Index < State->Workers.Num(); ++Index)
		{
			ALSTroopUnit* Worker = State->Workers[Index].Get();
			if (!Worker) continue;
			AAIController* WorkerAI = Cast<AAIController>(Worker->GetController());
			const UPathFollowingComponent* WorkerFollowing = WorkerAI ? WorkerAI->GetPathFollowingComponent() : nullptr;
			const FNavPathSharedPtr WorkerPath = WorkerFollowing ? WorkerFollowing->GetPath() : nullptr;
			State->Test->AddInfo(FString::Printf(TEXT("BreachWorker %s index=%d actor=%s location=%s velocity=%s order=%d queued=%d yielding=%d engaged=%s attackTarget=%s canMove=%d canControl=%d tick=%d knockedBack=%d movementMode=%d AIstatus=%d failure=%d endpoint=%s"), Stage,
				Index, *Worker->GetName(), *Worker->GetActorLocation().ToString(), *Worker->GetVelocity().ToString(), static_cast<int32>(Worker->Orders->GetOrderKind()), Worker->Orders->GetQueuedCount(),
				Worker->Orders->IsYieldingToCastle(), *GetNameSafe(Worker->Orders->GetEngagedTarget()), *GetNameSafe(Worker->BasicAttack->GetAttackTarget()), Worker->Orders->CanMove(),
				Worker->Selection->CanPlayerControl(), Worker->Orders->IsComponentTickEnabled(), Worker->Orders->IsKnockedBack(), static_cast<int32>(Worker->GetCharacterMovement()->MovementMode),
				WorkerAI ? static_cast<int32>(WorkerAI->GetMoveStatus()) : -1, static_cast<int32>(Worker->Orders->GetLastFailure()),
				WorkerPath.IsValid() && !WorkerPath->GetPathPoints().IsEmpty() ? *WorkerPath->GetPathPoints().Last().Location.ToString() : TEXT("none")));
		}
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBreachCalibrationCastleOverlap), false, Castle);
		TArray<FOverlapResult> Overlaps;
		State->World->OverlapMultiByObjectType(Overlaps, Castle->GetActorLocation(), Castle->GetActorQuat(), Objects,
			FCollisionShape::MakeCapsule(Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() + 2, Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), Query);
		for (const FOverlapResult& Hit : Overlaps)
			State->Test->AddInfo(FString::Printf(TEXT("BreachCastleContact %s actor=%s component=%s blocking=%d"), Stage, *GetPathNameSafe(Hit.GetActor()), *GetPathNameSafe(Hit.GetComponent()), Hit.bBlockingHit));
	}
	void LogDebugBodySweeps(const TCHAR* Stage)
	{
		const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("LS.RTS.InputDebug"));
		if (!Debug || Debug->GetInt() == 0) return;
		TArray<APawn*> Pawns{State->Castle.Get()};
		for (const TWeakObjectPtr<ALSTroopUnit>& Worker : State->Workers) if (Worker.IsValid()) Pawns.Add(Worker.Get());
		for (APawn* Pawn : Pawns)
		{
			UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
			ULSOrderComponent* Orders = Pawn->FindComponentByClass<ULSOrderComponent>();
			ULSBasicAttackComponent* OwnAttack = Pawn->FindComponentByClass<ULSBasicAttackComponent>();
			AAIController* AI = Cast<AAIController>(Pawn->GetController());
			UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
			const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
			const int32 StartIndex = Following ? static_cast<int32>(Following->GetCurrentPathIndex()) : -1;
			const int32 EndIndex = Following ? static_cast<int32>(Following->GetNextPathIndex()) : -1;
			const FVector SegmentStart = Path.IsValid() && Path->GetPathPoints().IsValidIndex(StartIndex) ? Path->GetPathPoints()[StartIndex].Location : Pawn->GetActorLocation();
			const FVector SegmentEnd = Path.IsValid() && Path->GetPathPoints().IsValidIndex(EndIndex) ? Path->GetPathPoints()[EndIndex].Location : Pawn->GetActorLocation();
			const FVector Target = Following && Following->GetStatus() != EPathFollowingStatus::Idle ? Following->GetCurrentTargetLocation() : Pawn->GetActorLocation();
			const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
			const FVector TowardTarget = (Target - Pawn->GetActorLocation()).GetSafeNormal2D();
			State->Test->AddInfo(FString::Printf(TEXT("BreachYieldTrace %s HP=%d crew=%d trial=%d world=%.6f sinceMove=%.6f actor=%s at=%s velocity=%s forward=%s nativeDirection=%s AIstatus=%d segment=%d/%d segmentStart=%s segmentEnd=%s currentTarget=%s order=%d yield=%d engaged=%s attackTarget=%s"),
				Stage, State->HP, State->Crew, State->TrialIndex, State->World->GetTimeSeconds(), State->World->GetTimeSeconds() - State->Current->MoveStart,
				*Pawn->GetName(), *Pawn->GetActorLocation().ToString(), *Pawn->GetVelocity().ToString(), *Forward.ToString(),
				Following && Following->GetStatus() != EPathFollowingStatus::Idle ? *Following->GetCurrentDirection().ToString() : TEXT("none"),
				AI ? static_cast<int32>(AI->GetMoveStatus()) : -1, StartIndex, EndIndex, *SegmentStart.ToString(), *SegmentEnd.ToString(), *Target.ToString(),
				Orders ? static_cast<int32>(Orders->GetOrderKind()) : -1, Orders && Orders->IsYieldingToCastle(), *GetNameSafe(Orders ? Orders->GetEngagedTarget() : nullptr),
				*GetNameSafe(OwnAttack ? OwnAttack->GetAttackTarget() : nullptr)));
			if (!Capsule) continue;
			FCollisionObjectQueryParams Objects;
			for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, LSCollisionChannels::Unit, LSCollisionChannels::Building, LSCollisionChannels::PathObstacle, LSCollisionChannels::Ground}) Objects.AddObjectTypesToQuery(Channel);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBreachYieldTrace), false, Pawn);
			const FVector Directions[] = {Forward, TowardTarget};
			for (int32 DirectionIndex = 0; DirectionIndex < 2; ++DirectionIndex)
			{
				if (Directions[DirectionIndex].IsNearlyZero() || (DirectionIndex == 1 && Directions[1].Equals(Directions[0], 0.001))) continue;
				TArray<FHitResult> Hits;
				State->World->SweepMultiByObjectType(Hits, Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + Directions[DirectionIndex] * 100,
					Capsule->GetComponentQuat(), Objects, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
				int32 Blocking = 0;
				for (const FHitResult& Hit : Hits)
				{
					UPrimitiveComponent* Body = Hit.GetComponent();
					AActor* Other = Hit.GetActor();
					if (!Body || Body->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block || Capsule->GetCollisionResponseToChannel(Body->GetCollisionObjectType()) != ECR_Block) continue;
					++Blocking;
					const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Other);
					const ULSOrderComponent* OtherOrders = Other ? Other->FindComponentByClass<ULSOrderComponent>() : nullptr;
					const ULSBasicAttackComponent* Attack = Other ? Other->FindComponentByClass<ULSBasicAttackComponent>() : nullptr;
					State->Test->AddInfo(FString::Printf(TEXT("BreachYieldBlock actor=%s sweep=%s blocker=%s component=%s mobility=%d distance=%.6f initialOverlap=%d hit=%s faction=%d blockerAt=%s blockerVelocity=%s order=%d yield=%d engaged=%s attackTarget=%s"),
						*Pawn->GetName(), DirectionIndex == 0 ? TEXT("actorForward100cm") : TEXT("nativeTarget100cm"), *GetPathNameSafe(Other), *Body->GetPathName(), static_cast<int32>(Body->Mobility),
						Hit.Distance, Hit.bStartPenetrating, *Hit.ImpactPoint.ToString(), Faction ? static_cast<int32>(Faction->GetFaction()) : -1,
						Other ? *Other->GetActorLocation().ToString() : TEXT("none"), Other ? *Other->GetVelocity().ToString() : TEXT("none"),
						OtherOrders ? static_cast<int32>(OtherOrders->GetOrderKind()) : -1, OtherOrders && OtherOrders->IsYieldingToCastle(),
						*GetNameSafe(OtherOrders ? OtherOrders->GetEngagedTarget() : nullptr), *GetNameSafe(Attack ? Attack->GetAttackTarget() : nullptr)));
				}
				State->Test->AddInfo(FString::Printf(TEXT("BreachYieldSweep actor=%s sweep=%s length=100cm capsule=%.3f/%.3f mutualBlockers=%d"),
					*Pawn->GetName(), DirectionIndex == 0 ? TEXT("actorForward") : TEXT("nativeTarget"), Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(), Blocking));
			}
		}
	}
	void ResetTrial()
	{
		State->Castle->Orders->SubmitOrder(FLSOrder());
		State->Castle->StopMoving();
		for (const TWeakObjectPtr<ALSTroopUnit>& Worker : State->Workers) if (Worker.IsValid()) Worker->Destroy();
		State->Workers.Reset();
		if (State->Gate.IsValid()) State->Gate->Destroy();
		State->Gate.Reset();
		State->Castle->SetActorLocationAndRotation(CastleStage, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
	}
	void RecordTrial()
	{
		const FLSBreachObservation& Value = *State->Current;
		State->Results.Add(ObservationJson(Value));
		State->Test->AddInfo(FString::Printf(TEXT("Actual breach HP=%d crew=%d trial=%d first=%.3f death=%.3f collisionOff=%.3f navReady=%.3f move=%.3f fullClear=%.3f elapsed=%.3f yield=%d"),
			Value.HP, Value.Crew, Value.TrialIndex, Value.FirstDamage, Value.DeathDamage, Value.CollisionOff, Value.NavigationReady, Value.MoveStart, Value.FullClear, Value.FullClear - Value.FirstDamage, Value.bSawYield));
		WriteCalibrationReport(State);
	}
	double Elapsed() const { return FPlatformTime::Seconds() - PhaseStarted; }
	void Advance(int32 Next) { Phase = Next; PhaseStarted = FPlatformTime::Seconds(); }
	bool Wait(const TCHAR* What, double Limit)
	{
		if (Elapsed() < Limit) return false;
		State->StopReason = What;
		LogMovementSnapshot(TEXT("timeout"));
		State->Test->AddError(FString::Printf(TEXT("HP=%d crew=%d %s timed out; HP tuning stopped to expose fixture/navigation/movement failure. castle=%s order=%d receiverId=%lld firstDamage=%.9f deathDamage=%.9f collisionOff=%.9f hasFirst=%d hasDeath=%d"), State->HP, State->Crew, What,
			State->Castle.IsValid() ? *State->Castle->GetActorLocation().ToString() : TEXT("none"), State->Castle.IsValid() ? static_cast<int32>(State->Castle->Orders->GetOrderKind()) : -1,
			State->Current ? State->Current->GateReceiverId : 0, State->Current ? State->Current->FirstDamage : -1,
			State->Current ? State->Current->DeathDamage : -1, State->Current ? State->Current->CollisionOff : -1,
			State->Current && State->Current->FirstDamage >= 0, State->Current && State->Current->DeathDamage >= 0));
		return Finish();
	}
	bool Fail(const TCHAR* What) { State->StopReason = What; State->Test->AddError(What); return Finish(); }
	bool Finish()
	{
		State->bFinished = true;
		WriteCalibrationReport(State);
		if (State->World.IsValid() && State->DestroyedHandle.IsValid()) State->World->RemoveOnActorDestroyedHandler(State->DestroyedHandle);
		State->DestroyedHandle.Reset();
		GEditor->RequestEndPlayMap();
		Advance(90);
		return false;
	}
	TSharedRef<FLSBreachCalibrationState> State;
	TUniquePtr<FStartPIEForAutomationCommand> Starter;
	int32 Phase = 0;
	double PhaseStarted = FPlatformTime::Seconds();
};

class FLSBreachCalibrationCleanup : public IAutomationLatentCommand
{
public:
	explicit FLSBreachCalibrationCleanup(TSharedRef<FLSBreachCalibrationState> InState) : State(InState) {}
	virtual bool Update() override
	{
		if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestEndPlayMap();
			State->Test->AddError(TEXT("Cannot restore calibration template because PIE did not end within its bounded cleanup phase"));
			return true;
		}
		if (State->EditorCopy.IsValid()) State->EditorCopy->GetOutermost()->SetDirtyFlag(false);
		State->Test->TestTrue(TEXT("Restore original editor map"), State->OriginalFilename.IsEmpty()
			? FAutomationEditorCommonUtils::CreateNewMap() != nullptr : FEditorFileUtils::LoadMap(State->OriginalFilename, false, false));
		State->Test->TestTrue(TEXT("Calibration leaves source map bytes unchanged"), FMD5Hash::HashFile(*State->SourceFilename) == State->SourceHash);
		return true;
	}
private:
	TSharedRef<FLSBreachCalibrationState> State;
};

bool OpenBreachTemplate(const TSharedRef<FLSBreachCalibrationState>& State)
{
	FAutomationTestBase& Test = *State->Test;
	if (!Test.TestTrue(TEXT("Breach fixture needs idle editor"), GEditor && !GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress())) return false;
	UWorld* Original = GEditor->GetEditorWorldContext().World();
	if (Original)
		for (ULevel* Level : Original->GetLevels()) if (!Test.TestFalse(TEXT("Breach fixture refuses an unsaved editor map"), Level->GetOutermost()->IsDirty())) return false;
	if (Original) FPackageName::DoesPackageExist(Original->GetOutermost()->GetName(), &State->OriginalFilename);
	if (!Test.TestTrue(TEXT("Actual map exists"), FPackageName::DoesPackageExist(TEXT("/Game/Landship/Maps/L_BattlefieldPrototype"), &State->SourceFilename))) return false;
	State->SourceHash = FMD5Hash::HashFile(*State->SourceFilename);
	if (!Test.TestTrue(TEXT("Open actual map as unsaved template"), FEditorFileUtils::LoadMap(State->SourceFilename, true, false))) return false;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	State->EditorCopy = World;
	return true;
}

bool PrepareIsolatedBreachTemplate(const TSharedRef<FLSBreachCalibrationState>& State, const FVector& CastleLocation)
{
	if (!OpenBreachTemplate(State)) return false;
	FAutomationTestBase& Test = *State->Test;
	UWorld* World = State->EditorCopy.Get();
	ALSBattlefieldSetup* Setup = nullptr;
	int32 SetupCount = 0;
	for (TActorIterator<ALSBattlefieldSetup> It(World); It; ++It) { Setup = *It; ++SetupCount; }
	if (!Test.TestTrue(TEXT("Template preserves its unique configured battlefield setup and Core"), SetupCount == 1 && Setup && IsValid(Setup->CoreOutpost))) return false;
	ALSOutpost* Core = Setup->CoreOutpost.Get();
	TArray<AActor*> Remove;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->IsA<ALSTroopUnit>() || It->IsA<ALSPathObstacle>() || It->IsA<ALSCastle>() ||
			It->IsA<ALSBattlefieldEncounterCoordinator>() || It->IsA<ALSBattlefieldLayoutAuthoring>() || (It->IsA<ALSOutpost>() && *It != Core)) Remove.Add(*It);
	for (AActor* Actor : Remove) World->EditorDestroyActor(Actor, false);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, CastleLocation);
	if (!Test.TestNotNull(TEXT("Native actual castle created in template"), Castle)) return false;
	Castle->AIControllerClass = ALSCastleAIController::StaticClass();
	Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500, false);
	ULSCastleMovementComponent* Movement = CastChecked<ULSCastleMovementComponent>(Castle->GetMovementComponent());
	Movement->NavAgentProps.AgentRadius = CastleNavigationRadius;
	Movement->NavAgentProps.AgentHeight = 1000;
	Movement->bTurnBeforeForward = true;
	Movement->TurnRate = 90;
	Movement->ForwardAcceleration = 300;
	Movement->ForwardBrakingDeceleration = 400;
	Setup->PlayerCastle = Castle;
	State->bReady = true;
	return true;
}

class FLSCrowdedShoulderEscapeCommand : public IAutomationLatentCommand
{
public:
	using FBodyPathQuery = TFunction<bool(const ULSOrderComponent*, const TArray<FVector>&)>;
	using FOriginHeightQuery = TFunction<bool(const ULSOrderComponent*, double&)>;
	FLSCrowdedShoulderEscapeCommand(TSharedRef<FLSBreachCalibrationState> InState, FBodyPathQuery InBodyPathQuery, FOriginHeightQuery InOriginHeightQuery, bool bInForwardEscapeScenario = false)
		: State(InState), BodyPathQuery(MoveTemp(InBodyPathQuery)), OriginHeightQuery(MoveTemp(InOriginHeightQuery)), bForwardEscapeScenario(bInForwardEscapeScenario) {}
	virtual bool Update() override
	{
		if (Phase == 90)
		{
			if (!GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress()) return true;
			if (Elapsed() >= 30) { State->Test->AddError(TEXT("Crowded shoulder PIE did not end")); return true; }
			return false;
		}
		if (Phase == 0)
		{
			if (!ProfilesReady(State->EditorCopy.Get())) return Wait(TEXT("Both editor profiles must finish before crowded shoulder PIE"), 60);
			FRequestPlaySessionParams Params;
			Params.bAllowOnlineSubsystem = false;
			Starter = MakeUnique<FStartPIEForAutomationCommand>(Params);
			Advance(1);
		}
		if (Phase == 1)
		{
			if (!Starter->Update()) return false;
			Starter.Reset();
			State->World = GEditor->PlayWorld;
			if (!State->World.IsValid() || !State->World->HasBegunPlay()) return Fail(TEXT("Crowded shoulder PIE did not begin"));
			for (TActorIterator<ALSCastle> It(State->World.Get()); It; ++It) State->Castle = *It;
			int32 SetupCount = 0;
			for (TActorIterator<ALSBattlefieldSetup> It(State->World.Get()); It; ++It) { ++SetupCount; State->Core = It->CoreOutpost.Get(); }
			ULSBattlefieldStateSubsystem* Battlefield = State->World->GetSubsystem<ULSBattlefieldStateSubsystem>();
			FLSPlayerCastleState Ownership;
			if (SetupCount != 1 || !State->Core.IsValid() || !State->Castle.IsValid() || !Battlefield || !Battlefield->IsSetupValid() ||
				!Battlefield->TryGetPlayerCastleState(Ownership) || Ownership.Castle.Get() != State->Castle.Get() || Battlefield->GetCoreOutpost() != State->Core.Get())
				return Fail(TEXT("Crowded shoulder fixture requires actual PIE Core/setup/castle ownership"));
			AAIController* AI = Cast<AAIController>(State->Castle->GetController());
			ULSCastleMovementComponent* Movement = Cast<ULSCastleMovementComponent>(State->Castle->GetMovementComponent());
			FLSCastleStatRow Stats;
			if (!AI || !Cast<ULSCastlePathFollowingComponent>(AI->GetPathFollowingComponent()) ||
				State->Castle->GetClass() != ALSCastle::StaticClass() || State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() != 500 ||
				State->Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() != 500 || !Movement ||
				Movement->NavAgentProps.AgentRadius != CastleNavigationRadius || Movement->NavAgentProps.AgentHeight != 1000 || !Movement->bTurnBeforeForward ||
				Movement->TurnRate != 90 || Movement->ForwardAcceleration != 300 || Movement->ForwardBrakingDeceleration != 400 ||
				!State->Castle->TryGetStats(Stats) || Stats.MovementSpeed != 400 || Stats.Health != 1000 || Stats.VisionRadius != 1600)
				return Fail(TEXT("Crowded shoulder fixture requires native castle500 and its actual path following component"));
			Advance(2);
		}
		if (!State->World.IsValid()) return Fail(TEXT("Crowded shoulder PIE ended unexpectedly"));
		if (Phase == 2)
		{
			const bool bProfilesReady = ProfilesReady(State->World.Get());
			if (!bProfilesReady || !HasCompleteCastlePath(State->World.Get(), State->Castle.Get()))
			{
				if (!bLoggedOpenPathProbe && Elapsed() >= 1)
				{
					bLoggedOpenPathProbe = true;
					ReportOpenCastlePathProbe(*State->Test, State->World.Get(), State->Castle.Get(), bProfilesReady);
				}
				return Wait(TEXT("Crowded shoulder open corridor must have complete castle path"), 30);
			}
			if (!bLoggedOpenPathProbe)
			{
				bLoggedOpenPathProbe = true;
				ReportOpenCastlePathProbe(*State->Test, State->World.Get(), State->Castle.Get(), bProfilesReady);
			}
			const TArray<FVector> Positions = bForwardEscapeScenario
				? TArray<FVector>{FVector(6166.735, -690.159, 90), FVector(6138, -500, 90), FVector(6138, -300, 90), FVector(6138, -100, 90),
					FVector(6138, 100, 90), FVector(6138, 300, 90), FVector(6138, 500, 90), FVector(6166.735, 690.159, 90)}
				: TArray<FVector>{FVector(6138, -100, 90), FVector(6138, 100, 90),
					FVector(6138, -657.56, 90), FVector(6138, 657.56, 90), FVector(6250.8, -657.7, 90), FVector(6250.8, 657.7, 90)};
			for (const FVector& Position : Positions)
			{
				const FTransform Transform(FRotator::ZeroRotator, Position);
				ALSTroopUnit* Worker = State->World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Worker) return Fail(TEXT("Crowded shoulder native spearman spawn failed"));
				Worker->SetInitialFaction(ELSFaction::Ally);
				Worker->SetInitialTroopType(ELSTroopType::Spearman);
				Worker->FinishSpawning(Transform);
				State->Workers.Add(Worker);
			}
			Advance(3);
			return false;
		}
		if (Phase == 3)
		{
			if (!ProfilesReady(State->World.Get())) return Wait(TEXT("Crowded shoulder worker profiles must be ready"), 30);
			if (!CheckWorkers()) return Finish();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				ALSTroopUnit* Worker = State->Workers[CentralIndex(Index)].Get();
				CentralStarts[Index] = Worker->GetActorLocation();
				WitnessTargets[Index] = bForwardEscapeScenario ? FVector(6884, Index == 0 ? -662 : 662, 0) : FVector(5994, Index == 0 ? -900 : 900, 0);
				if (!CheckWitness(Worker, WitnessTargets[Index]))
					return Fail(TEXT("Read-only native escape witness invalid; fixture does not establish a solver defect"));
			}
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = CastleGoal;
			if (!State->Castle->Orders->SubmitOrder(Move)) return Fail(TEXT("Crowded shoulder public castle Move rejected"));
			Advance(4);
			return false;
		}
		if (Phase == 4)
		{
			if (!CheckWorkers()) return Finish();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				ALSTroopUnit* Worker = State->Workers[CentralIndex(Index)].Get();
				const bool bYielding = Worker->Orders->IsYieldingToCastle();
				SawYield[Index] |= bYielding;
				if (!bForwardEscapeScenario || !bYielding) continue;
				AAIController* AI = Cast<AAIController>(Worker->GetController());
				const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
				const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
				if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial() || Path->GetPathPoints().IsEmpty()) return Fail(TEXT("Forward escape observation needs the native executing yield path"));
				const FVector Endpoint = Path->GetEndLocation();
				const double ForwardProgress = FVector::DotProduct(Endpoint - CentralStarts[Index], (CastleGoal - CastleStage).GetSafeNormal2D());
				if (!LoggedYield[Index])
				{
					LoggedYield[Index] = true;
					State->Test->AddInfo(FString::Printf(TEXT("CrowdedForwardChoice worker=%s start=%s actualExecutingEnd=%s alongCastleAdvance=%.6f witness=%s time=%.6f; endpoint observed through native path follower"),
						*Worker->GetName(), *CentralStarts[Index].ToString(), *Endpoint.ToString(), ForwardProgress, *WitnessTargets[Index].ToString(), Elapsed()));
				}
				if (ForwardProgress < -KINDA_SMALL_NUMBER)
				{
					if (!CheckWitness(Worker, WitnessTargets[Index])) return Fail(TEXT("Forward witness became invalid at opposing yield observation; fixture cannot establish a solver defect"));
					return Fail(TEXT("Central idle worker chose yield endpoint west of its start despite a valid native forward escape"));
				}
			}
			if (State->Castle->GetActorLocation().X - State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() >= GateFarFace)
			{
				if (FMath::Abs(State->Castle->GetActorLocation().Y) > GateHalfWidth - State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius())
					return Fail(TEXT("Crowded castle full-clear center must remain inside the real opening strip"));
				State->Test->TestTrue(TEXT("Both central idle workers yield through native orders"), SawYield[0] && SawYield[1]);
				State->Test->AddInfo(FString::Printf(TEXT("Crowded shoulder full sphere clearance at %.3fs castle=%s centralYield=%d/%d"), Elapsed(), *State->Castle->GetActorLocation().ToString(), SawYield[0], SawYield[1]));
				return Finish();
			}
			return Wait(TEXT("Both central workers must yield and actual castle500 fully cross far face6570"), 15);
		}
		return false;
	}
private:
	int32 CentralIndex(int32 Index) const { return bForwardEscapeScenario ? Index + 3 : Index; }
	bool CheckWorkers()
	{
		for (const TWeakObjectPtr<ALSTroopUnit>& WeakWorker : State->Workers)
		{
			const ALSTroopUnit* Worker = WeakWorker.Get();
			if (!Worker || !ULSCombatTargetLibrary::IsCombatAlive(Worker) || Worker->Orders->GetOrderKind() != ELSOrderKind::Idle ||
				Worker->Orders->GetQueuedCount() != 0 || Worker->Orders->GetEngagedTarget() || Worker->BasicAttack->GetAttackTarget())
			{
				State->Test->AddError(TEXT("Crowded shoulder workers must retain native idle orders, empty queues and no combat targets"));
				return false;
			}
		}
		return true;
	}
	bool CheckWitness(ALSTroopUnit* Worker, const FVector& Requested)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
		const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Worker->GetNavAgentPropertiesRef(), Worker->GetNavAgentLocation()) : nullptr;
		FNavLocation Projected;
		const bool bProjected = Data && Nav->ProjectPointToNavigation(Requested, Projected, FVector(150, 150, 300), Data) && FVector::Dist2D(Requested, Projected.Location) <= 60;
		if (!bProjected) { State->Test->AddInfo(FString::Printf(TEXT("CrowdedWitness worker=%s requested=%s projection=0"), *Worker->GetName(), *Requested.ToString())); return false; }
		const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(Worker, Projected.Location, LSMineAvoidance::Snapshot(State->World.Get()));
		const bool bComplete = Safe.Result == ELSMinePathResult::SafePath && Safe.Path.IsValid() && Safe.Path->IsValid() && !Safe.Path->IsPartial();
		double OriginHeight;
		if (!OriginHeightQuery(Worker->Orders, OriginHeight)) return false;
		const FVector Offset(0, 0, OriginHeight);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCrowdedShoulderWitness), false, Worker);
		float Radius, Height;
		Worker->GetSimpleCollisionCylinder(Radius, Height);
		const bool bEmpty = !State->World->OverlapAnyTestByObjectType(Projected.Location + Offset, FQuat::Identity, Objects,
			FCollisionShape::MakeCapsule(Radius + 4, FMath::Max(Height, Radius + 4)), Query);
		TArray<FVector> BodyCenters{Worker->GetActorLocation()};
		if (bComplete) for (const FNavPathPoint& Point : Safe.Path->GetPathPoints()) BodyCenters.Add(Point.Location + Offset);
		const bool bClear = bComplete && BodyPathQuery(Worker->Orders, BodyCenters);
		State->Test->AddInfo(FString::Printf(TEXT("CrowdedWitness worker=%s requested=%s projected=%s nativeOriginHeight=%.3f fullSafePath=%d endpointEmpty=%d nativeBodyPathClear=%d points=%d"),
			*Worker->GetName(), *Requested.ToString(), *Projected.Location.ToString(), OriginHeight, bComplete, bEmpty, bClear, BodyCenters.Num()));
		return bComplete && bEmpty && bClear;
	}
	double Elapsed() const { return FPlatformTime::Seconds() - PhaseStarted; }
	void Advance(int32 Next) { Phase = Next; PhaseStarted = FPlatformTime::Seconds(); }
	bool Wait(const TCHAR* What, double Limit) { if (Elapsed() < Limit) return false; return Fail(What); }
	bool Fail(const TCHAR* What)
	{
		State->Test->AddError(FString::Printf(TEXT("%s; phase=%d elapsed=%.3f castle=%s order=%d centralYield=%d/%d"), What, Phase, Elapsed(),
			State->Castle.IsValid() ? *State->Castle->GetActorLocation().ToString() : TEXT("none"), State->Castle.IsValid() ? static_cast<int32>(State->Castle->Orders->GetOrderKind()) : -1, SawYield[0], SawYield[1]));
		for (const TWeakObjectPtr<ALSTroopUnit>& Worker : State->Workers) if (Worker.IsValid())
			State->Test->AddInfo(FString::Printf(TEXT("CrowdedWorker actor=%s at=%s yielding=%d order=%d engaged=%s target=%s"), *Worker->GetName(), *Worker->GetActorLocation().ToString(),
				Worker->Orders->IsYieldingToCastle(), static_cast<int32>(Worker->Orders->GetOrderKind()), *GetNameSafe(Worker->Orders->GetEngagedTarget()), *GetNameSafe(Worker->BasicAttack->GetAttackTarget())));
		return Finish();
	}
	bool Finish() { GEditor->RequestEndPlayMap(); Advance(90); return false; }
	TSharedRef<FLSBreachCalibrationState> State;
	FBodyPathQuery BodyPathQuery;
	FOriginHeightQuery OriginHeightQuery;
	TUniquePtr<FStartPIEForAutomationCommand> Starter;
	bool bForwardEscapeScenario = false;
	bool bLoggedOpenPathProbe = false;
	bool SawYield[2] = {false, false};
	bool LoggedYield[2] = {false, false};
	FVector CentralStarts[2];
	FVector WitnessTargets[2];
	int32 Phase = 0;
	double PhaseStarted = FPlatformTime::Seconds();
};

class FLSCastleAutomaticCornerCommand : public IAutomationLatentCommand
{
public:
	explicit FLSCastleAutomaticCornerCommand(TSharedRef<FLSBreachCalibrationState> InState) : State(InState) {}
	virtual bool Update() override
	{
		if (Phase == 90)
		{
			if (!GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress()) return true;
			if (Elapsed() < 30) return false;
			State->Test->AddError(TEXT("Automatic corner PIE did not end"));
			return true;
		}
		if (Phase == 0)
		{
			if (!ProfilesReady(State->EditorCopy.Get())) return Wait(TEXT("Both editor profiles must finish before automatic corner PIE"), 60);
			FRequestPlaySessionParams Params;
			Params.bAllowOnlineSubsystem = false;
			Starter = MakeUnique<FStartPIEForAutomationCommand>(Params);
			Advance(1);
		}
		if (Phase == 1)
		{
			if (!Starter->Update()) return false;
			Starter.Reset();
			State->World = GEditor->PlayWorld;
			if (!State->World.IsValid() || !State->World->HasBegunPlay()) return Fail(TEXT("Automatic corner PIE did not begin"));
			for (TActorIterator<ALSCastle> It(State->World.Get()); It; ++It) State->Castle = *It;
			ULSBattlefieldStateSubsystem* Battlefield = State->World->GetSubsystem<ULSBattlefieldStateSubsystem>();
			FLSPlayerCastleState Ownership;
			if (!State->Castle.IsValid() || !Battlefield || !Battlefield->IsSetupValid() || !Battlefield->TryGetPlayerCastleState(Ownership) ||
				Ownership.Castle.Get() != State->Castle.Get() || !IsValid(Battlefield->GetCoreOutpost()))
				return Fail(TEXT("Automatic corner requires isolated actual castle/Core/setup ownership"));
			AAIController* AI = Cast<AAIController>(State->Castle->GetController());
			if (!AI || !Cast<ULSCastlePathFollowingComponent>(AI->GetPathFollowingComponent()) ||
				State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() != 500 || State->Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() != 500)
				return Fail(TEXT("Automatic corner requires actual sphere500 and native castle path follower"));
			Advance(2);
		}
		if (!State->World.IsValid()) return Fail(TEXT("Automatic corner PIE ended unexpectedly"));
		if (Phase == 2)
		{
			if (!ProfilesReady(State->World.Get())) return Wait(TEXT("Both actual corner profiles must finish"), 30);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
			const ANavigationData* Data = Nav->GetNavDataForProps(State->Castle->GetNavAgentPropertiesRef(), State->Castle->GetNavAgentLocation());
			if (!Data || Data->GetConfig().AgentRadius != CastleNavigationRadius || Data->GetConfig().AgentHeight != 1000)
				return Fail(TEXT("Automatic corner must query exact registered580/1000 NavData"));
			FNavLocation From, To;
			if (!Nav->ProjectPointToNavigation(State->Castle->GetNavAgentLocation(), From, FVector(150, 150, 300), Data) ||
				!Nav->ProjectPointToNavigation(Goal, To, FVector(150, 150, 300), Data) || FVector::Dist2D(Goal, To.Location) > 150)
				return Fail(TEXT("Automatic corner actual start/goal must project on580 logical profile"));
			OriginHeight = State->Castle->GetActorLocation().Z - From.Location.Z;
			auto FreshPath = [&]()
			{
				FPathFindingQuery Query(State->Castle.Get(), *Data, From.Location, To.Location);
				Query.SetAllowPartialPaths(false);
				return Nav->FindPathSync(State->Castle->GetNavAgentPropertiesRef(), Query);
			};
			const FPathFindingResult Raw = FreshPath();
			const FPathFindingResult Offset = FreshPath();
			if (!Complete(Raw.Path) || !Complete(Offset.Path) || Raw.Path == Offset.Path)
				return Fail(TEXT("Automatic corner experiment requires two independent complete native paths"));
			FNavMeshPath* OffsetMesh = Offset.Path->CastPath<FNavMeshPath>();
			if (!OffsetMesh) return Fail(TEXT("Automatic corner offset experiment requires native mesh path"));
			OffsetMesh->OffsetFromCorners(50);
			const bool bRawClear = SweepStaticPath(TEXT("freshRawNav580"), Raw.Path);
			const bool bOffsetClear = SweepStaticPath(TEXT("freshOffset50"), Offset.Path);
			State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner experiment NavData=%s profile=%.1f/%.1f nativeOriginHeight=%.3f rawClear=%d offset50CandidateClear=%d"),
				*Data->GetName(), Data->GetConfig().AgentRadius, Data->GetConfig().AgentHeight, OriginHeight, bRawClear, bOffsetClear));
			if (!CheckQueryCases(Raw.Path)) return Fail(TEXT("Automatic corner bounded compatibility/narrow/mine query checks failed"));
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = Goal;
			if (!State->Castle->Orders->SubmitOrder(Move)) return Fail(TEXT("Automatic corner single public Move rejected"));
			AAIController* AI = CastChecked<AAIController>(State->Castle->GetController());
			const FNavPathSharedPtr PublicPath = AI->GetPathFollowingComponent()->GetPath();
			State->Test->TestTrue(TEXT("Public automatic corner path clears actual500 sphere against all static bodies"), Complete(PublicPath) && SweepStaticPath(TEXT("publicMove"), PublicPath));
			if (!BeginRepath()) return Fail(TEXT("Automatic corner requires an executing native path for invalidation"));
			return false;
		}
		if (Phase == 4)
		{
			if (!RepathPath.IsValid() || RepathPath->GetTimeStamp() <= RepathTimestamp || !RepathPath->IsUpToDate())
				return Wait(TEXT("Native invalidated corner path must finish repath"), 5);
			AAIController* AI = CastChecked<AAIController>(State->Castle->GetController());
			if (AI->GetPathFollowingComponent()->GetPath() != RepathPath || !Complete(RepathPath)) return Fail(TEXT("Native repath must remain the executing complete path"));
			const FPathFindingQueryData RebuiltQuery = RepathPath->GetQueryData();
			const FNavPathSharedPtr Expected = QueryPath(State->Castle.Get(), RebuiltQuery.StartLocation, RebuiltQuery.EndLocation);
			FNavMeshPath* ExpectedMesh = Complete(Expected) ? Expected->CastPath<FNavMeshPath>() : nullptr;
			if (!ExpectedMesh) return Fail(TEXT("Native repath comparison requires fresh independent mesh path"));
			ExpectedMesh->OffsetFromCorners(50);
			if (!RepathPath->GetEndLocation().Equals(RepathGoal, 0.1) || !SamePoints(RepathPath, Expected))
				return Fail(TEXT("Native repath must preserve destination and equal one offset of freshly rebuilt raw path"));
			if (!SweepStaticPath(bDirectRun ? TEXT("directRepath") : TEXT("ordersRepath"), RepathPath)) return Fail(TEXT("Native repath must retain actual500 static clearance"));
			++RepathCount;
			State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner nativeInvalidation mode=%s pass=%d unchangedEnd=%s noAccumulatedOffset=1"),
				bDirectRun ? TEXT("RequestMove") : TEXT("Orders"), RepathCount, *RepathGoal.ToString()));
			if (RepathCount < 2) { if (!BeginRepath()) return Fail(TEXT("Second native path invalidation unavailable")); }
			else Advance(3);
			return false;
		}
		if (Phase == 3)
		{
			if (FVector::Dist2D(State->Castle->GetActorLocation(), Goal) <= 30)
			{
				State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner mode=%s actual center arrived within30cm in %.3fs location=%s"),
					bDirectRun ? TEXT("RequestMove") : TEXT("Orders"), Elapsed(), *State->Castle->GetActorLocation().ToString()));
				if (bDirectRun) return Finish();
				State->Castle->Orders->SubmitOrder(FLSOrder());
				State->Castle->StopMoving();
				State->Castle->SetActorLocationAndRotation(FVector(0, 0, 500), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
				bDirectRun = true;
				RepathCount = 0;
				Advance(5);
				return false;
			}
			return Wait(TEXT("Single automatic castle Move S-to-LA must reach center within30cm"), 30);
		}
		if (Phase == 5)
		{
			if (!ProfilesReady(State->World.Get())) return Wait(TEXT("Direct RequestMove reset requires actual profiles ready"), 30);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
			const ANavigationData* Data = Nav->GetNavDataForProps(State->Castle->GetNavAgentPropertiesRef(), State->Castle->GetNavAgentLocation());
			FNavLocation Here;
			if (!Data || !Nav->ProjectPointToNavigation(State->Castle->GetNavAgentLocation(), Here, FVector(150, 150, 300), Data)) return Fail(TEXT("Direct RequestMove reset start must project"));
			OriginHeight = State->Castle->GetActorLocation().Z - Here.Location.Z;
			if (!State->Castle->RequestMove(Goal)) return Fail(TEXT("Actual castle direct RequestMove rejected"));
			AAIController* AI = CastChecked<AAIController>(State->Castle->GetController());
			const FNavPathSharedPtr Direct = AI->GetPathFollowingComponent()->GetPath();
			if (!Complete(Direct) || !SweepStaticPath(TEXT("directRequestMove"), Direct)) return Fail(TEXT("Direct RequestMove must provide complete actual500 static clearance"));
			if (!BeginRepath()) return Fail(TEXT("Direct RequestMove native invalidation unavailable"));
			return false;
		}
		return false;
	}
private:
	bool Complete(const FNavPathSharedPtr& Path) const { return Path.IsValid() && Path->IsValid() && !Path->IsPartial() && Path->GetPathPoints().Num() >= 2; }
	FNavPathSharedPtr QueryPath(APawn* Pawn, const FVector& Start, const FVector& End) const
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
		const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Pawn->GetNavAgentLocation()) : nullptr;
		if (!Data) return nullptr;
		FPathFindingQuery Query(Pawn, *Data, Start, End);
		Query.SetAllowPartialPaths(false);
		return Nav->FindPathSync(Pawn->GetNavAgentPropertiesRef(), Query).Path;
	}
	bool SamePoints(const FNavPathSharedPtr& A, const FNavPathSharedPtr& B) const
	{
		if (!Complete(A) || !Complete(B) || A->GetPathPoints().Num() != B->GetPathPoints().Num()) return false;
		for (int32 Index = 0; Index < A->GetPathPoints().Num(); ++Index)
			if (!A->GetPathPoints()[Index].Location.Equals(B->GetPathPoints()[Index].Location, 0.1)) return false;
		return true;
	}
	bool BeginRepath()
	{
		AAIController* AI = CastChecked<AAIController>(State->Castle->GetController());
		RepathPath = AI->GetPathFollowingComponent()->GetPath();
		if (!Complete(RepathPath)) return false;
		RepathGoal = RepathPath->GetEndLocation();
		RepathTimestamp = RepathPath->GetTimeStamp();
		RepathPath->EnableRecalculationOnInvalidation(true);
		RepathPath->Invalidate();
		Advance(4);
		return true;
	}
	bool CheckQueryCases(const FNavPathSharedPtr& Raw)
	{
		ALSCastle* Castle = State->Castle.Get();
		ULSCastleMovementComponent* Movement = CastChecked<ULSCastleMovementComponent>(Castle->GetMovementComponent());
		Movement->bTurnBeforeForward = false;
		const FLSMineSafePath Legacy = LSMineAvoidance::BuildSafePath(Castle, Goal, {});
		const bool bLegacyDirectAccepted = Castle->RequestMove(Goal);
		AAIController* AI = CastChecked<AAIController>(Castle->GetController());
		const FNavPathSharedPtr LegacyDirect = AI->GetPathFollowingComponent()->GetPath();
		const FPathFindingQueryData LegacyQuery = Complete(LegacyDirect) ? LegacyDirect->GetQueryData() : FPathFindingQueryData();
		const FNavPathSharedPtr LegacyRaw = Complete(LegacyDirect) ? QueryPath(Castle, LegacyQuery.StartLocation, LegacyQuery.EndLocation) : nullptr;
		const bool bLegacyUnchanged = SamePoints(Raw, Legacy.Path) && bLegacyDirectAccepted && SamePoints(LegacyRaw, LegacyDirect);
		Castle->StopMoving();
		Movement->bTurnBeforeForward = true;
		if (!State->Test->TestTrue(TEXT("Defaultfalse castle mine query and direct request preserve raw native path"), bLegacyUnchanged)) return false;
		const FTransform Transform(FRotator::ZeroRotator, FVector(0, 1200, 90));
		ALSTroopUnit* Troop = State->World->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Troop) return false;
		Troop->SetInitialFaction(ELSFaction::Ally);
		Troop->SetInitialTroopType(ELSTroopType::Spearman);
		Troop->FinishSpawning(Transform);
		const FLSMineSafePath TroopSafe = LSMineAvoidance::BuildSafePath(Troop, Goal, {});
		const FNavPathSharedPtr TroopRaw = Complete(TroopSafe.Path) ? QueryPath(Troop, TroopSafe.Path->GetStartLocation(), TroopSafe.Destination) : nullptr;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->World.Get());
		const ANavigationData* TroopData = Nav->GetNavDataForProps(Troop->GetNavAgentPropertiesRef(), Troop->GetNavAgentLocation());
		const bool bSameTroopPoints = SamePoints(TroopSafe.Path, TroopRaw);
		State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner troopCompatibility body=%.3f/%.3f pawnNav=%.3f/%.3f selectedData=%s selectedProfile=%.3f/%.3f safeResult=%d safePoints=%d rawPoints=%d samePoints=%d"),
			Troop->GetCapsuleComponent()->GetScaledCapsuleRadius(), Troop->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			Troop->GetNavAgentPropertiesRef().AgentRadius, Troop->GetNavAgentPropertiesRef().AgentHeight, *GetNameSafe(TroopData),
			TroopData ? TroopData->GetConfig().AgentRadius : -1, TroopData ? TroopData->GetConfig().AgentHeight : -1,
			static_cast<int32>(TroopSafe.Result), TroopSafe.Path.IsValid() ? TroopSafe.Path->GetPathPoints().Num() : 0,
			TroopRaw.IsValid() ? TroopRaw->GetPathPoints().Num() : 0, bSameTroopPoints));
		if (!bSameTroopPoints)
			for (const FNavPathSharedPtr& Path : {TroopSafe.Path, TroopRaw}) if (Path.IsValid())
				for (int32 Index = 0; Index < FMath::Min(12, Path->GetPathPoints().Num()); ++Index)
					State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner troopCompatibility %s point=%d nav=%s"),
						Path == TroopSafe.Path ? TEXT("safe") : TEXT("raw"), Index, *Path->GetPathPoints()[Index].Location.ToString()));
		const bool bTroopUnchanged = bSameTroopPoints && TroopData && TroopData->GetConfig().AgentRadius == 142 && TroopData->GetConfig().AgentHeight == 284;
		Troop->Destroy();
		if (!State->Test->TestTrue(TEXT("Troop142 mine query preserves raw native path"), bTroopUnchanged)) return false;
		for (const FVector& Destination : {FVector(5200, -1000, 0), FVector(5800, 5000, 0)})
		{
			const FLSMineSafePath Narrow = LSMineAvoidance::BuildSafePath(Castle, Destination, {});
			State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner narrowQuery requested=%s projected=%s result=%d complete=%d"),
				*Destination.ToString(), *Narrow.Destination.ToString(), static_cast<int32>(Narrow.Result), Complete(Narrow.Path)));
			if (Narrow.Result != ELSMinePathResult::SafePath || !Complete(Narrow.Path) || FVector::Dist2D(Narrow.Destination, Destination) > 150 ||
				!SweepStaticPath(Destination.Y < 0 ? TEXT("center16mMouth") : TEXT("right22mMouth"), Narrow.Path)) return false;
		}
		const TArray<FLSMineSphere> Mines{{FVector(1250, 0, 88), 250}};
		const FVector MineGoal(2500, 0, 0);
		const FLSMineSafePath Detour = LSMineAvoidance::BuildSafePath(Castle, MineGoal, Mines);
		State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner syntheticMineQuery requested=%s projected=%s result=%d complete=%d"),
			*MineGoal.ToString(), *Detour.Destination.ToString(), static_cast<int32>(Detour.Result), Complete(Detour.Path)));
		if (Detour.Result != ELSMinePathResult::SafePath || !Complete(Detour.Path) || FVector::Dist2D(Detour.Destination, MineGoal) > 150 ||
			!SweepStaticPath(TEXT("syntheticMineQueryDetour"), Detour.Path)) return false;
		const TArray<FNavPathPoint>& Points = Detour.Path->GetPathPoints();
		const FVector Offset(0, 0, OriginHeight);
		if (!LSMineAvoidance::CapsuleSegmentTouchesSphere(Points[0].Location + Offset, Points.Last().Location + Offset, 500, 500, Mines[0])) return false;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
			if (!LSMineAvoidance::IsCapsuleSegmentSafe(Points[Index - 1].Location + Offset, Points[Index].Location + Offset, 500, 500, Mines[0])) return false;
		State->Test->AddInfo(FString::Printf(TEXT("AutomaticCorner boundedQueries defaultfalseUnchanged=1 troop142Unchanged=1 narrowStaticClear=2 syntheticMineDetourClear=1 detourPoints=%d; synthetic hazard tests path queries only"), Points.Num()));
		return true;
	}
	bool SweepStaticPath(const TCHAR* Label, const FNavPathSharedPtr& Path)
	{
		bool bClear = true;
		UCapsuleComponent* Capsule = State->Castle->GetCapsuleComponent();
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCastleAutomaticCorner), false, State->Castle.Get());
		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			State->Test->AddInfo(FString::Printf(TEXT("AutomaticCornerPath %s point=%d nav=%s"), Label, Index, *Points[Index].Location.ToString()));
			if (Index == 0) continue;
			TArray<FHitResult> Hits;
			State->World->SweepMultiByObjectType(Hits, Points[Index - 1].Location + FVector(0, 0, OriginHeight),
				Points[Index].Location + FVector(0, 0, OriginHeight), FQuat::Identity, Objects, FCollisionShape::MakeSphere(Capsule->GetScaledCapsuleRadius()), Query);
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* Body = Hit.GetComponent();
				if (!Body || Body->Mobility != EComponentMobility::Static ||
					Body->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block || Capsule->GetCollisionResponseToChannel(Body->GetCollisionObjectType()) != ECR_Block) continue;
				bClear = false;
				State->Test->AddInfo(FString::Printf(TEXT("AutomaticCornerStaticHit %s segment=%d actor=%s component=%s at=%s impact=%s distance=%.6f initialOverlap=%d"),
					Label, Index, *GetNameSafe(Hit.GetActor()), *Body->GetPathName(), *Hit.Location.ToString(), *Hit.ImpactPoint.ToString(), Hit.Distance, Hit.bStartPenetrating));
			}
		}
		return bClear;
	}
	double Elapsed() const { return FPlatformTime::Seconds() - PhaseStarted; }
	void Advance(int32 Next) { Phase = Next; PhaseStarted = FPlatformTime::Seconds(); }
	bool Wait(const TCHAR* What, double Limit) { if (Elapsed() < Limit) return false; return Fail(What); }
	bool Fail(const TCHAR* What)
	{
		State->Test->AddError(FString::Printf(TEXT("%s; phase=%d elapsed=%.3f castle=%s velocity=%s order=%d"), What, Phase, Elapsed(),
			State->Castle.IsValid() ? *State->Castle->GetActorLocation().ToString() : TEXT("none"), State->Castle.IsValid() ? *State->Castle->GetVelocity().ToString() : TEXT("none"),
			State->Castle.IsValid() ? static_cast<int32>(State->Castle->Orders->GetOrderKind()) : -1));
		return Finish();
	}
	bool Finish() { GEditor->RequestEndPlayMap(); Advance(90); return false; }
	TSharedRef<FLSBreachCalibrationState> State;
	TUniquePtr<FStartPIEForAutomationCommand> Starter;
	const FVector Goal = FVector(3000, -6300, 0);
	FNavPathSharedPtr RepathPath;
	FVector RepathGoal = FVector::ZeroVector;
	double RepathTimestamp = 0;
	int32 RepathCount = 0;
	bool bDirectRun = false;
	double OriginHeight = 0;
	int32 Phase = 0;
	double PhaseStarted = FPlatformTime::Seconds();
};

struct FLSAuthoredCastleLeg
{
	FString Name;
	FVector From;
	FVector To;
};

enum class ELSCastleNavGeometryState : uint8
{
	GatesOpen,
	GatesIntact,
	AllDestructibleFixturesRemoved
};

const TCHAR* CastleNavGeometryStateName(ELSCastleNavGeometryState GeometryState)
{
	switch (GeometryState)
	{
	case ELSCastleNavGeometryState::GatesOpen: return TEXT("GatesOpen");
	case ELSCastleNavGeometryState::GatesIntact: return TEXT("GatesIntact");
	case ELSCastleNavGeometryState::AllDestructibleFixturesRemoved: return TEXT("AllDestructibleFixturesRemoved");
	}
	return TEXT("Unknown");
}

class FLSAuthoredCastleLegQueryCommand : public IAutomationLatentCommand
{
public:
	FLSAuthoredCastleLegQueryCommand(TSharedRef<FLSBreachCalibrationState> InState, TArray<FLSAuthoredCastleLeg> InGateLegs, ELSCastleNavGeometryState InGeometryState = ELSCastleNavGeometryState::GatesOpen)
		: State(InState), Legs(MoveTemp(InGateLegs)), GeometryState(InGeometryState)
	{
		if (GeometryState != ELSCastleNavGeometryState::GatesOpen) { Legs.Reset(); return; }
		const FVector S(0, 0, 0), J0(2500, 0, 0), J2(18200, 0, 0), PreCore(18500, 1800, 0), F(20000, 1800, 0);
		const FVector XL(10500, -6300, 0), XC(10500, 0, 0), XR(10500, 4600, 0);
		const TArray<FVector> Routes[] = {
			{S, J0, FVector(3000, -6300, 0), FVector(4000, -6300, 0), FVector(4600, -7300, 0), XL, FVector(15600, -5500, 0), J2, PreCore, F},
			{S, J0, FVector(4000, 0, 0), FVector(4500, 0, 0), XC, FVector(16100, 700, 0), J2, PreCore, F},
			{S, J0, FVector(3000, 4600, 0), FVector(4000, 4600, 0), FVector(4500, 5100, 0), XR, FVector(12500, 5100, 0), FVector(13050, 4600, 0), FVector(15600, 3800, 0), J2, PreCore, F},
			{XL, XC, XR}};
		const TCHAR* Names[] = {TEXT("Left macro"), TEXT("Center macro"), TEXT("Right macro"), TEXT("Crossroad")};
		for (int32 Route = 0; Route < UE_ARRAY_COUNT(Routes); ++Route)
			for (int32 Segment = 1; Segment < Routes[Route].Num(); ++Segment)
				AddBoth(FString::Printf(TEXT("%s leg%d"), Names[Route], Segment), Routes[Route][Segment - 1], Routes[Route][Segment]);
		const FVector Mouths[] = {FVector(4000, -6300, 0), FVector(4000, 0, 0), FVector(4000, 4600, 0)};
		const FVector Bays[] = {FVector(5200, -7200, 0), FVector(5200, -4600, 0), FVector(5200, -1000, 0), FVector(5400, 700, 0), FVector(5800, 5000, 0), FVector(5800, 3900, 0)};
		for (int32 Lane = 0; Lane < 3; ++Lane)
		{
			AddBoth(FString::Printf(TEXT("Lane%d J0-to-mouth"), Lane), J0, Mouths[Lane]);
			for (int32 Bay = Lane * 2; Bay < Lane * 2 + 2; ++Bay)
			{
				const FVector Approach = Bay == 1 ? FVector(5550, -6300, 0) : Mouths[Lane];
				if (Bay == 1) AddBoth(TEXT("Left upper mouth-to-stage"), Mouths[Lane], Approach);
				AddBoth(FString::Printf(TEXT("Combat bay%d"), Bay), Approach, Bays[Bay]);
			}
		}
	}
	virtual bool Update() override
	{
		UWorld* World = State->EditorCopy.Get();
		if (!World || !State->Castle.IsValid()) { State->Test->AddError(TEXT("Authored-leg query template missing")); return true; }
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!ProfilesReady(World) || !Nav || Nav->HasDirtyAreasQueued() || Nav->GetNumRemainingBuildTasks() != 0)
		{
			if (FPlatformTime::Seconds() - Started < 60) return false;
			State->Test->AddError(TEXT("Authored-leg query template navigation did not finish within60s"));
			return true;
		}
		ALSCastle* Castle = State->Castle.Get();
		const ANavigationData* Data = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
		if (!State->Test->TestTrue(TEXT("Authored-leg queries use logical580/1000 profile for physical500 body"), Data && Data->GetConfig().AgentRadius == CastleNavigationRadius && Data->GetConfig().AgentHeight == 1000)) return true;
		const FTransform OriginalTransform = Castle->GetActorTransform();
		FNavLocation Here;
		if (!Nav->ProjectPointToNavigation(Castle->GetNavAgentLocation(), Here, FVector(150, 150, 300), Data)) { State->Test->AddError(TEXT("Authored query original castle origin must project")); return true; }
		const double OriginHeight = Castle->GetActorLocation().Z - Here.Location.Z;
		TMap<NavNodeRef, FString> LegEndpointPolys;
		State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanOrigin capturedBeforeLegQueries=%s poly=%llu"), *Here.Location.ToString(), static_cast<uint64>(Here.NodeRef)));
		int32 ClearLegs = 0, CappedPortals = 0;
		for (const FLSAuthoredCastleLeg& Leg : Legs)
		{
			if ((Leg.Name.StartsWith(TEXT("B_Gate_L2")) || Leg.Name.StartsWith(TEXT("B_Gate_C2")) || Leg.Name.StartsWith(TEXT("B_Gate_R2"))) && Leg.Name.EndsWith(TEXT("open forward")))
				ReportEndpoint(Leg.Name + TEXT(" rejected calibration-only far+12m endpoint"), Leg.To + (Leg.To - Leg.From).GetSafeNormal2D() * (1200 - CastleNavigationRadius - 150), Nav, Data);
			FNavLocation From, To;
			const bool bFrom = Nav->ProjectPointToNavigation(Leg.From, From, FVector(150, 150, 300), Data);
			const bool bTo = Nav->ProjectPointToNavigation(Leg.To, To, FVector(150, 150, 300), Data);
			if (!bFrom || !bTo || FVector::Dist2D(Leg.From, From.Location) > 150 || FVector::Dist2D(Leg.To, To.Location) > 150)
			{
				State->Test->AddError(Leg.Name + TEXT(" requires logical580 endpoint projections within150cm"));
				ReportEndpoint(Leg.Name + TEXT(" from"), Leg.From, Nav, Data);
				ReportEndpoint(Leg.Name + TEXT(" to"), Leg.To, Nav, Data);
				continue;
			}
			LegEndpointPolys.Add(From.NodeRef, Leg.Name + TEXT(" from"));
			LegEndpointPolys.Add(To.NodeRef, Leg.Name + TEXT(" to"));
			Castle->SetActorLocation(From.Location + FVector(0, 0, OriginHeight), false, nullptr, ETeleportType::TeleportPhysics);
			const FLSMineSafePath Safe = LSMineAvoidance::BuildSafePath(Castle, To.Location, {});
			const bool bComplete = Safe.Result == ELSMinePathResult::SafePath && Safe.Path.IsValid() && Safe.Path->IsValid() && !Safe.Path->IsPartial() && Safe.Path->GetPathPoints().Num() >= 2;
			const bool bUnchangedEnds = bComplete && Safe.Path->GetStartLocation().Equals(From.Location, 0.1) &&
				Safe.Destination.Equals(To.Location, 0.1) && Safe.Path->GetEndLocation().Equals(To.Location, 0.1);
			State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastleLeg %s from=%s to=%s safeResult=%d complete=%d unchangedEnds=%d points=%d; query-only, no locomotion claim"),
				*Leg.Name, *From.Location.ToString(), *To.Location.ToString(), static_cast<int32>(Safe.Result), bComplete, bUnchangedEnds, Safe.Path.IsValid() ? Safe.Path->GetPathPoints().Num() : 0));
			if (!State->Test->TestTrue(Leg.Name + TEXT(" production complete path preserves projected endpoints"), bUnchangedEnds)) continue;
			FPathFindingQuery RawQuery(Castle, *Data, From.Location, To.Location);
			RawQuery.SetAllowPartialPaths(false);
			const FPathFindingResult Raw = Nav->FindPathSync(Castle->GetNavAgentPropertiesRef(), RawQuery);
			if (FNavMeshPath* Mesh = Raw.Path.IsValid() ? Raw.Path->CastPath<FNavMeshPath>() : nullptr)
			{
				const TArray<FNavPathPoint>& Corners = Mesh->GetPathPoints();
				const TArray<FNavigationPortalEdge>& Edges = Mesh->GetPathCorridorEdges();
				TSet<int32> Reported;
				for (int32 Corner = 1; Corner < Corners.Num() - 1; ++Corner)
					for (int32 Edge = 0; Edge < Edges.Num(); ++Edge)
					{
						const double HalfWidth = Edges[Edge].GetLength() * 0.5;
						if (HalfWidth >= 50 || Reported.Contains(Edge) || FMath::PointDistToSegmentSquared(Corners[Corner].Location, Edges[Edge].Left, Edges[Edge].Right) > 0.01 ||
							FMath::Min(FVector::DistSquared(Corners[Corner].Location, Edges[Edge].Left), FVector::DistSquared(Corners[Corner].Location, Edges[Edge].Right)) > 2500) continue;
						Reported.Add(Edge);
						++CappedPortals;
						State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastlePortalCap %s rawCorner=%d at=%s portal=%d halfWidth=%.6f belowRequested50cm"),
							*Leg.Name, Corner, *Corners[Corner].Location.ToString(), Edge, HalfWidth));
					}
			}
			bool bClear = true;
			FCollisionObjectQueryParams Objects;
			for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, LSCollisionChannels::Unit, LSCollisionChannels::Building, LSCollisionChannels::PathObstacle}) Objects.AddObjectTypesToQuery(Channel);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSAuthoredCastleLeg), false, Castle);
			const UCapsuleComponent* Capsule = Castle->GetCapsuleComponent();
			const TArray<FNavPathPoint>& Points = Safe.Path->GetPathPoints();
			for (int32 Segment = 1; Segment < Points.Num(); ++Segment)
			{
				TArray<FHitResult> Hits;
				World->SweepMultiByObjectType(Hits, Points[Segment - 1].Location + FVector(0, 0, OriginHeight), Points[Segment].Location + FVector(0, 0, OriginHeight),
					Capsule->GetComponentQuat(), Objects, FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
				for (const FHitResult& Hit : Hits)
				{
					const UPrimitiveComponent* Body = Hit.GetComponent();
					if (!Body || Body->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block || Capsule->GetCollisionResponseToChannel(Body->GetCollisionObjectType()) != ECR_Block) continue;
					bClear = false;
					State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastleLegHit %s segment=%d actor=%s component=%s mobility=%d from=%s to=%s impact=%s distance=%.6f initialOverlap=%d"),
						*Leg.Name, Segment, Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("none"), *Body->GetPathName(), static_cast<int32>(Body->Mobility),
						*Points[Segment - 1].Location.ToString(), *Points[Segment].Location.ToString(), *Hit.ImpactPoint.ToString(), Hit.Distance, Hit.bStartPenetrating));
				}
			}
			if (State->Test->TestTrue(Leg.Name + TEXT(" actual500 full-height capsule clears all mutually blocking retained geometry"), bClear)) ++ClearLegs;
		}
		ScanGeneratedPolygons(Cast<ARecastNavMesh>(Data), OriginHeight, Here.NodeRef, LegEndpointPolys);
		Castle->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
		State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastleLegSummary geometryState=%s queries=%d clear=%d rawCornerPortalsCappedBelow50=%d; no AI or locomotion exercised, fixture preservation stated separately"), CastleNavGeometryStateName(GeometryState), Legs.Num(), ClearLegs, CappedPortals));
		return true;
	}
private:
	void ScanGeneratedPolygons(const ARecastNavMesh* Recast, double OriginHeight, NavNodeRef OriginPoly, const TMap<NavNodeRef, FString>& LegEndpointPolys)
	{
		State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanState geometryState=%s queryOnlyEditorClone=%s; generated polygons sampled without gameplay or height-based acceptance changes"),
			CastleNavGeometryStateName(GeometryState), *GetPathNameSafe(State->EditorCopy.Get())));
		if (!State->Test->TestNotNull(TEXT("Generated Castle polygon scan requires native Recast"), Recast)) return;
		const double CellSize = Recast->GetCellSize(ENavigationDataResolution::Default);
		if (!State->Test->TestTrue(TEXT("Generated Castle default cell size is finite and positive"), FMath::IsFinite(CellSize) && CellSize > 0)) return;
		State->Test->TestEqual(TEXT("Generated Castle radius matches registered logical580"), Recast->AgentRadius, CastleNavigationRadius);
		State->Test->TestEqual(TEXT("Generated Castle height matches registered1000"), Recast->AgentHeight, 1000.0f);
		State->Test->TestEqual(TEXT("Castle uses its precision-specific NavData class before registration"), Recast->GetClass(), ALSBattlefieldCastleNavMesh::StaticClass());
		State->Test->TestEqual(TEXT("Castle-only simplification error is0.5"), Recast->MaxSimplificationError, 0.5f);
		State->Test->TestEqual(TEXT("Castle default cell size is10"), CellSize, 10.0);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->EditorCopy.Get());
		bool bTroopProfileFound = false;
		for (const FNavDataConfig& Profile : Nav->GetSupportedAgents())
			if (Profile.AgentRadius == 142 && Profile.AgentHeight == 284)
			{
				bTroopProfileFound = true;
				const ARecastNavMesh* Troops = Cast<ARecastNavMesh>(Nav->GetNavDataForProps(Profile));
				if (State->Test->TestNotNull(TEXT("Troop profile retains its registered native mesh"), Troops))
				{
					State->Test->TestEqual(TEXT("Troops retain base Recast class"), Troops->GetClass(), ARecastNavMesh::StaticClass());
					State->Test->TestEqual(TEXT("Troop simplification remains1.3"), Troops->MaxSimplificationError, 1.3f);
					State->Test->TestEqual(TEXT("Troop default cell size remains19"), Troops->GetCellSize(ENavigationDataResolution::Default), 19.0f);
					State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanTroopProvenance class=%s registeredRadius=%.3f registeredHeight=%.3f generationRadius=%.3f generationHeight=%.3f maxSimplificationError=%.6f"),
						*Troops->GetClass()->GetPathName(), Troops->GetConfig().AgentRadius, Troops->GetConfig().AgentHeight, Troops->AgentRadius, Troops->AgentHeight, Troops->MaxSimplificationError));
				}
			}
		State->Test->TestTrue(TEXT("Castle precision change preserves the actual troop profile"), bTroopProfileFound);
		State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanSettings class=%s data=%s registeredRadius=%.3f registeredHeight=%.3f generationRadius=%.3f generationHeight=%.3f defaultCellSize=%.6f defaultCellHeight=%.6f maxSimplificationError=%.6f COMPUTED_ceilGenerationRadiusOverDefaultCellSize=%d COMPUTED_roundedRadiusCm=%.6f COMPUTED_simplificationTimesCellSize=%.6f; computed values are not internal voxel-field readbacks"),
			*Recast->GetClass()->GetPathName(), *Recast->GetName(), Recast->GetConfig().AgentRadius, Recast->GetConfig().AgentHeight, Recast->AgentRadius, Recast->AgentHeight,
			CellSize, Recast->GetCellHeight(ENavigationDataResolution::Default), Recast->MaxSimplificationError,
			FMath::CeilToInt(Recast->AgentRadius / CellSize), FMath::CeilToInt(Recast->AgentRadius / CellSize) * CellSize, Recast->MaxSimplificationError * CellSize));
		const FSharedConstNavQueryFilter Filter = Recast->GetDefaultQueryFilter();
		if (!State->Test->TestTrue(TEXT("Generated Castle scan uses its actual default query filter"), Filter.IsValid())) return;
		float Costs[RECAST_MAX_AREAS], FixedCosts[RECAST_MAX_AREAS];
		Filter->GetAllAreaCosts(Costs, FixedCosts, RECAST_MAX_AREAS);
		TArray<FNavTileRef> Tiles;
		Recast->GetAllNavMeshTiles(Tiles);
		int32 GroundPolygons = 0, IncludedPolygons = 0, ExcludedFlags = 0, ExcludedAreas = 0, InvalidPolygons = 0;
		int32 VertexSamples = 0, EdgeSamples = 0, CenterSamples = 0, HitSamples = 0, Contacts = 0;
		int32 SupportContacts = 0, SupportSamples = 0, LoggedSupports = 0;
		double MinimumNavZ = TNumericLimits<double>::Max(), MaximumNavZ = TNumericLimits<double>::Lowest();
		const double ScanStarted = FPlatformTime::Seconds();
		TArray<FNavPoly> WalkablePolygons;
		TSet<NavNodeRef> Allowed, Reachable;
		for (FNavTileRef Tile : Tiles)
		{
			TArray<FNavPoly> Polygons;
			Recast->GetPolysInTile(Tile, Polygons);
			GroundPolygons += Polygons.Num();
			for (const FNavPoly& Poly : Polygons)
			{
				uint16 PolyFlags, AreaFlags;
				if (!Recast->IsNodeRefValid(Poly.Ref) || !Recast->GetPolyFlags(Poly.Ref, PolyFlags, AreaFlags)) { ++InvalidPolygons; continue; }
				if ((PolyFlags & Recast->GetNavLinkFlag()) || !(PolyFlags & Filter->GetIncludeFlags()) || (PolyFlags & Filter->GetExcludeFlags())) { ++ExcludedFlags; continue; }
				const uint32 Area = Recast->GetPolyAreaID(Poly.Ref);
				if (Area >= RECAST_MAX_AREAS) { ++InvalidPolygons; continue; }
				const UClass* AreaClass = Recast->GetAreaClass(Area);
				if (!AreaClass || AreaClass->IsChildOf(UNavArea_Null::StaticClass()) || !FMath::IsFinite(Costs[Area]) || !FMath::IsFinite(FixedCosts[Area]) ||
					Costs[Area] >= TNumericLimits<float>::Max() || FixedCosts[Area] >= TNumericLimits<float>::Max()) { ++ExcludedAreas; continue; }
				WalkablePolygons.Add(Poly);
				Allowed.Add(Poly.Ref);
			}
		}
		TArray<NavNodeRef> Queue;
		if (State->Test->TestTrue(TEXT("Original castle spawn polygon belongs to default-walkable graph"), Allowed.Contains(OriginPoly)))
		{
			Queue.Add(OriginPoly);
			Reachable.Add(OriginPoly);
		}
		int32 NeighborReadFailures = 0;
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			TArray<NavNodeRef> Neighbors;
			if (!Recast->GetPolyNeighbors(Queue[Index], Neighbors)) { ++NeighborReadFailures; continue; }
			for (NavNodeRef Neighbor : Neighbors)
				if (Allowed.Contains(Neighbor) && !Reachable.Contains(Neighbor)) { Reachable.Add(Neighbor); Queue.Add(Neighbor); }
		}
		int32 UnreachableEndpoints = 0;
		for (const TPair<NavNodeRef, FString>& Endpoint : LegEndpointPolys)
			if (!Reachable.Contains(Endpoint.Key))
			{
				++UnreachableEndpoints;
				State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanUnreachableLegEndpoint label=%s poly=%llu"), *Endpoint.Value, static_cast<uint64>(Endpoint.Key)));
			}
		State->Test->TestEqual(TEXT("Native Castle polygon graph adjacency reads remain valid"), NeighborReadFailures, 0);
		if (GeometryState == ELSCastleNavGeometryState::GatesOpen)
			State->Test->TestEqual(TEXT("All authored-leg projected endpoints are reachable from original castle spawn polygon"), UnreachableEndpoints, 0);
		int32 ComponentSamples[2] = {0, 0}, ComponentInvalidSamples[2] = {0, 0}, ComponentSupportSamples[2] = {0, 0};
		int32 ComponentContacts[2] = {0, 0}, ComponentSupportContacts[2] = {0, 0};
		TArray<FString> ReachableDetails, UnreachableDetails;
		struct FLSUnreachableHeightStats
		{
			int32 Samples = 0, Contacts = 0;
			double MaxPenetration = 0;
			double MinNormalZ = TNumericLimits<double>::Max(), MaxNormalZ = TNumericLimits<double>::Lowest();
		};
		TMap<double, FLSUnreachableHeightStats> UnreachableInvalidByNavZ;
		const UCapsuleComponent* Capsule = State->Castle->GetCapsuleComponent();
		const FCollisionShape BodyShape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		FCollisionObjectQueryParams Objects;
		for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, LSCollisionChannels::Unit, LSCollisionChannels::Building, LSCollisionChannels::PathObstacle}) Objects.AddObjectTypesToQuery(Channel);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCastleGeneratedPolyScan), false, State->Castle.Get());
		Query.bFindInitialOverlaps = true;
		auto CheckBody = [&](NavNodeRef PolyRef, const TCHAR* Kind, int32 Index, const FVector& From, const FVector& To)
		{
			const bool bReachable = Reachable.Contains(PolyRef);
			const int32 Component = bReachable ? 0 : 1;
			++ComponentSamples[Component];
			TArray<FHitResult> Hits;
			State->EditorCopy->SweepMultiByObjectType(Hits, From + FVector(0, 0, OriginHeight), To + FVector(0, 0, OriginHeight), Capsule->GetComponentQuat(), Objects, BodyShape, Query);
			bool bHit = false;
			bool bSupport = false;
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* Body = Hit.GetComponent();
				if (!Body || Body->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block || Capsule->GetCollisionResponseToChannel(Body->GetCollisionObjectType()) != ECR_Block) continue;
				++Contacts;
				++ComponentContacts[Component];
				const double BottomFrom = From.Z + OriginHeight - Capsule->GetScaledCapsuleHalfHeight();
				const double BottomTo = To.Z + OriginHeight - Capsule->GetScaledCapsuleHalfHeight();
				const double NormalDotUp = FVector::DotProduct(Hit.Normal, FVector::UpVector);
				const bool bSupportPlaneContact = Hit.PenetrationDepth <= KINDA_SMALL_NUMBER && NormalDotUp >= 1.0 - KINDA_SMALL_NUMBER &&
					FMath::Abs(BottomFrom - BottomTo) <= KINDA_SMALL_NUMBER && FMath::Abs(Hit.ImpactPoint.Z - BottomFrom) <= KINDA_SMALL_NUMBER;
				if (bSupportPlaneContact)
				{
					bSupport = true;
					++SupportContacts;
					++ComponentSupportContacts[Component];
					if (LoggedSupports++ < 8)
						State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanSupport reachableFromSpawn=%d poly=%llu kind=%s index=%d navFrom=%s navTo=%s actor=%s bodyBottom=%.9f impactZ=%.9f penetration=%.9f normalDotUp=%.9f normal=%s impactNormal=%s; penetration<=nativeKINDA, normalDotUp>=1-nativeKINDA, capsule-bottom/support-plane match within nativeKINDA; impact feature normal logged only; nativeKINDA_SMALL_NUMBER=%.9f"),
							bReachable, static_cast<uint64>(PolyRef), Kind, Index, *From.ToString(), *To.ToString(), Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("none"),
							BottomFrom, Hit.ImpactPoint.Z, Hit.PenetrationDepth, NormalDotUp, *Hit.Normal.ToString(), *Hit.ImpactNormal.ToString(), static_cast<double>(KINDA_SMALL_NUMBER)));
					continue;
				}
				bHit = true;
				if (!bReachable)
				{
					FLSUnreachableHeightStats& Height = UnreachableInvalidByNavZ.FindOrAdd(From.Z);
					++Height.Contacts;
					Height.MaxPenetration = FMath::Max(Height.MaxPenetration, static_cast<double>(Hit.PenetrationDepth));
					Height.MinNormalZ = FMath::Min(Height.MinNormalZ, Hit.Normal.Z);
					Height.MaxNormalZ = FMath::Max(Height.MaxNormalZ, Hit.Normal.Z);
				}
				TArray<FString>& Details = bReachable ? ReachableDetails : UnreachableDetails;
				if (Details.Num() < 24)
					Details.Add(FString::Printf(TEXT("CastlePolyScanHit reachableFromSpawn=%d poly=%llu kind=%s index=%d navFrom=%s navTo=%s bodyOriginHeight=%.6f actor=%s component=%s bounds=%s mobility=%d distance=%.6f initialOverlap=%d penetration=%.6f impact=%s normal=%s impactNormal=%s"),
						bReachable, static_cast<uint64>(PolyRef), Kind, Index, *From.ToString(), *To.ToString(), OriginHeight, Hit.GetActor() ? *Hit.GetActor()->GetActorLabel() : TEXT("none"), *Body->GetPathName(),
						*Body->Bounds.GetBox().ToString(), static_cast<int32>(Body->Mobility), Hit.Distance, Hit.bStartPenetrating, Hit.PenetrationDepth,
						*Hit.ImpactPoint.ToString(), *Hit.Normal.ToString(), *Hit.ImpactNormal.ToString()));
			}
			if (bHit)
			{
				++HitSamples;
				++ComponentInvalidSamples[Component];
				if (!bReachable) ++UnreachableInvalidByNavZ.FindOrAdd(From.Z).Samples;
			}
			if (bSupport) { ++SupportSamples; ++ComponentSupportSamples[Component]; }
		};
		for (const FNavPoly& Poly : WalkablePolygons)
		{
			TArray<FVector> Vertices;
			FVector Center;
			if (!Recast->GetPolyVerts(Poly.Ref, Vertices) || Vertices.Num() < 3 || !Recast->GetPolyCenter(Poly.Ref, Center)) { ++InvalidPolygons; continue; }
			++IncludedPolygons;
			for (int32 Vertex = 0; Vertex < Vertices.Num(); ++Vertex)
			{
				MinimumNavZ = FMath::Min(MinimumNavZ, Vertices[Vertex].Z);
				MaximumNavZ = FMath::Max(MaximumNavZ, Vertices[Vertex].Z);
				++VertexSamples;
				CheckBody(Poly.Ref, TEXT("vertex"), Vertex, Vertices[Vertex], Vertices[Vertex]);
				++EdgeSamples;
				CheckBody(Poly.Ref, TEXT("edge"), Vertex, Vertices[Vertex], Vertices[(Vertex + 1) % Vertices.Num()]);
			}
			++CenterSamples;
			CheckBody(Poly.Ref, TEXT("center"), 0, Center, Center);
		}
		for (const FString& Detail : ReachableDetails) State->Test->AddInfo(Detail);
		for (const FString& Detail : UnreachableDetails) State->Test->AddInfo(Detail);
		TArray<double> Heights;
		UnreachableInvalidByNavZ.GetKeys(Heights);
		Heights.Sort();
		int32 HistogramSamples = 0, HistogramContacts = 0;
		for (double NavZ : Heights)
		{
			const FLSUnreachableHeightStats& Height = UnreachableInvalidByNavZ.FindChecked(NavZ);
			HistogramSamples += Height.Samples;
			HistogramContacts += Height.Contacts;
			State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanUnreachableInvalidHeight geometryState=%s exactNavFromZ=%.9f invalidSamples=%d invalidContacts=%d maxPenetrationCm=%.9f minShapeNormalZ=%.9f maxShapeNormalZ=%.9f; report-only histogram, no height-based exclusion or tolerance change"),
				CastleNavGeometryStateName(GeometryState), NavZ, Height.Samples, Height.Contacts, Height.MaxPenetration, Height.MinNormalZ, Height.MaxNormalZ));
		}
		State->Test->TestEqual(TEXT("Unreachable-height report accounts for every invalid sample"), HistogramSamples, ComponentInvalidSamples[1]);
		State->Test->TestEqual(TEXT("Unreachable-height report accounts for every invalid contact"), HistogramContacts, ComponentContacts[1] - ComponentSupportContacts[1]);
		State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanSummary tilePoolCapacity=%d returnedTiles=%d groundPolygons=%d includedDefaultWalkablePolygons=%d excludedFlagOrLink=%d excludedArea=%d invalid=%d minNavZ=%.6f maxNavZ=%.6f vertices=%d completeEdges=%d centers=%d invalidCollisionSamples=%d invalidCollisionContacts=%d supportSamples=%d supportContacts=%d allMutualContacts=%d invalidDetailLimitPerComponent=24 supportDetailLimit=8 elapsed=%.6fs; all generated ground polygons, offmesh omitted by GetPolysInTile; physical500 full-height queries; support requires penetration<=nativeKINDA, normalDotUp>=1-nativeKINDA and capsule-bottom/impact plane match within nativeKINDA, without requiring primitive-feature ImpactNormal; horizontal contacts or penetration above nativeKINDA remain invalid; no locomotion claim"),
			Recast->GetNavMeshTilesCount(), Tiles.Num(), GroundPolygons, IncludedPolygons, ExcludedFlags, ExcludedAreas, InvalidPolygons,
			MinimumNavZ, MaximumNavZ, VertexSamples, EdgeSamples, CenterSamples, HitSamples, Contacts - SupportContacts, SupportSamples, SupportContacts, Contacts, FPlatformTime::Seconds() - ScanStarted));
		State->Test->TestTrue(TEXT("Generated Castle scan includes nonempty default-walkable polygons and no invalid references"), IncludedPolygons > 0 && InvalidPolygons == 0);
		State->Test->AddInfo(FString::Printf(TEXT("CastlePolyScanReachability originPoly=%llu graphAllowedPolygons=%d reachablePolygons=%d unreachablePolygons=%d legEndpointPolygons=%d unreachableLegEndpoints=%d reachableSamples=%d reachableInvalidSamples=%d reachableInvalidContacts=%d reachableSupportSamples=%d reachableSupportContacts=%d unreachableSamples=%d unreachableInvalidSamples=%d unreachableInvalidContacts=%d unreachableSupportSamples=%d unreachableSupportContacts=%d; native GetPolyNeighbors BFS constrained to default-filter ground polygons, no elevation heuristic; reachable invalid details emitted before unreachable details; no locomotion claim"),
			static_cast<uint64>(OriginPoly), Allowed.Num(), Reachable.Num(), Allowed.Num() - Reachable.Num(), LegEndpointPolys.Num(), UnreachableEndpoints,
			ComponentSamples[0], ComponentInvalidSamples[0], ComponentContacts[0] - ComponentSupportContacts[0], ComponentSupportSamples[0], ComponentSupportContacts[0],
			ComponentSamples[1], ComponentInvalidSamples[1], ComponentContacts[1] - ComponentSupportContacts[1], ComponentSupportSamples[1], ComponentSupportContacts[1]));
		State->Test->TestEqual(TEXT("Spawn-reachable Castle vertices, full edges and centers have no actual500 collision beyond native-tolerance support contact"), ComponentInvalidSamples[0], 0);
	}
	void ReportEndpoint(const FString& Label, const FVector& Requested, UNavigationSystemV1* Nav, const ANavigationData* Data)
	{
		FNavLocation Point, Wide;
		const bool bPoint = Nav->ProjectPointToNavigation(Requested, Point, FVector(150, 150, 300), Data);
		const bool bWide = Nav->ProjectPointToNavigation(Requested, Wide, FVector(1500, 1500, 300), Data);
		State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastleEndpoint %s requested=%s logicalRadius=%.1f project150=%d:%s drift=%.3f diagnosticOnlyWide=%d:%s wideDrift=%.3f"),
			*Label, *Requested.ToString(), Data->GetConfig().AgentRadius, bPoint, bPoint ? *Point.Location.ToString() : TEXT("none"), bPoint ? FVector::Dist2D(Requested, Point.Location) : -1,
			bWide, bWide ? *Wide.Location.ToString() : TEXT("none"), bWide ? FVector::Dist2D(Requested, Wide.Location) : -1));
		for (TActorIterator<ALSOutpost> It(State->EditorCopy.Get()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Requested) > 1800) continue;
			UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (!Body) continue;
			FVector Closest;
			const FVector IntendedBodyCenter = Requested + FVector(0, 0, State->Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const float Distance = Body->GetClosestPointOnCollision(IntendedBodyCenter, Closest);
			State->Test->AddInfo(FString::Printf(TEXT("AuthoredCastleEndpointFacility %s actor=%s at=%s component=%s bounds=%s mobility=%d navRelevant=%d intendedSphereCenter=%s closest=%s sphereCenterDistance=%.3f actualRadius=%.1f"),
				*Label, *It->GetActorLabel(), *It->GetActorLocation().ToString(), *Body->GetPathName(), *Body->Bounds.GetBox().ToString(), static_cast<int32>(Body->Mobility), Body->IsNavigationRelevant(),
				*IntendedBodyCenter.ToString(), Distance >= 0 ? *Closest.ToString() : TEXT("none"), Distance, State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius()));
		}
	}
	void AddBoth(const FString& Name, const FVector& From, const FVector& To)
	{
		Legs.Add({Name + TEXT(" forward"), From, To});
		Legs.Add({Name + TEXT(" reverse"), To, From});
	}
	TSharedRef<FLSBreachCalibrationState> State;
	TArray<FLSAuthoredCastleLeg> Legs;
	ELSCastleNavGeometryState GeometryState;
	double Started = FPlatformTime::Seconds();
};
bool QueueCastleNavGeometryState(FAutomationTestBase& Test, ELSCastleNavGeometryState GeometryState)
{
	if (!Test.TestEqual(TEXT("Castle NavData class default precision is0.5 before map load"), GetDefault<ALSBattlefieldCastleNavMesh>()->MaxSimplificationError, 0.5f) ||
		!Test.TestEqual(TEXT("Base Recast class default precision remains1.3 before map load"), GetDefault<ARecastNavMesh>()->MaxSimplificationError, 1.3f) ||
		!Test.TestEqual(TEXT("Castle NavData class default cell size is10 before map load"), GetDefault<ALSBattlefieldCastleNavMesh>()->GetCellSize(ENavigationDataResolution::Default), 10.0f) ||
		!Test.TestEqual(TEXT("Base Recast class default cell size remains19 before map load"), GetDefault<ARecastNavMesh>()->GetCellSize(ENavigationDataResolution::Default), 19.0f)) return false;
	TSharedRef<FLSBreachCalibrationState> State = MakeShared<FLSBreachCalibrationState>();
	State->Test = &Test;
	if (!OpenBreachTemplate(State)) return false;
	UWorld* World = State->EditorCopy.Get();
	TArray<FLSAuthoredCastleLeg> GateLegs;
	TSet<AActor*> Gates;
	int32 OutpostsBefore = 0, TurretsBefore = 0;
	for (TActorIterator<ALSOutpost> It(World); It; ++It) ++OutpostsBefore;
	for (TActorIterator<ALSTurret> It(World); It; ++It) ++TurretsBefore;
	for (TActorIterator<ALSBattlefieldLayoutAuthoring> It(World); It; ++It)
		for (const FLSBattlefieldLayoutActorEntry& Entry : It->Registry)
			if (Entry.ActorId.ToString().StartsWith(TEXT("B_Gate_")))
			{
				ALSBarricade* Gate = Cast<ALSBarricade>(Entry.Actor.Get());
				UBoxComponent* Box = Gate ? Cast<UBoxComponent>(Gate->GetObstacleBody()) : nullptr;
				if (!Box || Gates.Contains(Gate)) continue;
				Gates.Add(Gate);
				if (GeometryState == ELSCastleNavGeometryState::GatesOpen)
				{
					const FVector Extent = Box->GetScaledBoxExtent();
					const FVector Normal = Extent.X <= Extent.Y ? Box->GetForwardVector() : Box->GetRightVector();
					const double CrossingOffset = FMath::Min(Extent.X, Extent.Y) + CastleNavigationRadius + 150;
					FVector From = Box->GetComponentLocation() - Normal * CrossingOffset;
					FVector To = Box->GetComponentLocation() + Normal * CrossingOffset;
					From.Z = To.Z = 0;
					if (Entry.ActorId == TEXT("B_Gate_Core")) { From = FVector(18500, 1800, 0); To = FVector(20000, 1800, 0); }
					Test.AddInfo(FString::Printf(TEXT("AuthoredCastleGateOpening id=%s bodyBounds=%s halfExtent=%s normal=%s logicalRadius=%.1f endpointMargin=150 from=%s to=%s"),
						*Entry.ActorId.ToString(), *Box->Bounds.GetBox().ToString(), *Extent.ToString(), *Normal.ToString(), CastleNavigationRadius, *From.ToString(), *To.ToString()));
					GateLegs.Add({Entry.ActorId.ToString() + TEXT(" open forward"), From, To});
					GateLegs.Add({Entry.ActorId.ToString() + TEXT(" open reverse"), To, From});
				}
			}
	int32 SetupCount = 0;
	ALSBattlefieldSetup* Setup = nullptr;
	for (TActorIterator<ALSBattlefieldSetup> It(World); It; ++It) { ++SetupCount; Setup = *It; State->Castle = Cast<ALSCastle>(It->PlayerCastle.Get()); State->Core = It->CoreOutpost.Get(); }
	const ULSCastleMovementComponent* Movement = State->Castle.IsValid() ? Cast<ULSCastleMovementComponent>(State->Castle->GetMovementComponent()) : nullptr;
	const bool bValid = Test.TestTrue(TEXT("Authored-leg template retains source castle/Core/setup and exactly seven gate IDs"), SetupCount == 1 && Setup && Setup->GetConfigurationError().IsEmpty() && State->Castle.IsValid() && State->Core.IsValid() && Gates.Num() == 7 &&
		Movement && Movement->bTurnBeforeForward && State->Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() == 500 && State->Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() == 500);
	if (bValid)
	{
		TArray<AActor*> Remove;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const bool bGameplayActor = It->IsA<ALSTroopUnit>() || It->IsA<ALSMine>() || It->IsA<ALSBattlefieldEncounterCoordinator>();
			const bool bGeometryRemoved = GeometryState == ELSCastleNavGeometryState::GatesOpen ? Gates.Contains(*It) :
				GeometryState == ELSCastleNavGeometryState::AllDestructibleFixturesRemoved && (It->IsA<ALSPathObstacle>() || It->IsA<ALSOutpost>());
			if (bGameplayActor || bGeometryRemoved) Remove.Add(*It);
		}
		for (AActor* Actor : Remove) World->EditorDestroyActor(Actor, false);
		int32 OutpostsAfter = 0, TurretsAfter = 0, BarricadesAfter = 0;
		for (TActorIterator<ALSOutpost> It(World); It; ++It) ++OutpostsAfter;
		for (TActorIterator<ALSTurret> It(World); It; ++It) ++TurretsAfter;
		for (TActorIterator<ALSBarricade> It(World); It; ++It) ++BarricadesAfter;
		int32 RemainingPathObstacles = 0;
		for (TActorIterator<ALSPathObstacle> It(World); It; ++It) ++RemainingPathObstacles;
		const bool bAllRemoved = GeometryState == ELSCastleNavGeometryState::AllDestructibleFixturesRemoved;
		const bool bExpectedGeometry = OutpostsBefore > 0 && TurretsBefore == 18 && (bAllRemoved
			? OutpostsAfter == 0 && TurretsAfter == 0 && BarricadesAfter == 0 && RemainingPathObstacles == 0
			: OutpostsAfter == OutpostsBefore && TurretsAfter == TurretsBefore && BarricadesAfter == (GeometryState == ELSCastleNavGeometryState::GatesIntact ? 25 : 18) && Setup->GetConfigurationError().IsEmpty());
		Test.AddInfo(FString::Printf(TEXT("CastleNavGeometryFixture state=%s outpostsBefore=%d after=%d turretsBefore=%d after=%d remainingBarricades=%d remainingPathObstacles=%d; source setup validated before mutation, all-removed state is editor-query-only without post-removal Core/setup/gameplay claim; prescribed state only, not all destruction combinations"),
			CastleNavGeometryStateName(GeometryState), OutpostsBefore, OutpostsAfter, TurretsBefore, TurretsAfter, BarricadesAfter, RemainingPathObstacles));
		if (Test.TestTrue(TEXT("Geometry template preserves exactly its explicitly selected fixture state"), bExpectedGeometry))
			FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSAuthoredCastleLegQueryCommand>(State, MoveTemp(GateLegs), GeometryState));
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
	return true;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldBreachCalibrationTest,
	"Landship.Maps.Battlefield.ActualCastleBreachCalibration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldBreachCalibrationTest::RunTest(const FString& Parameters)
{
	TSharedRef<FLSBreachCalibrationState> State = MakeShared<FLSBreachCalibrationState>();
	State->Test = this;
	State->ProgressReportFilename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("task48-actual-breach-calibration.json"));
	const FString CompletedDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Task48/BreachCalibration"));
	if (!TestTrue(TEXT("Create run-specific calibration output directory"), IFileManager::Get().MakeDirectory(*CompletedDirectory, true))) return false;
	State->CompletedReportFilename = CompletedDirectory / (State->RunId + TEXT(".completed.json"));
	if (!PrepareIsolatedBreachTemplate(State, CastleStage))
	{
		if (State->EditorCopy.IsValid())
			FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCommand>(State));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSIdleYieldCrowdedShoulderEscapeTest,
	"Landship.Maps.Battlefield.IdleYieldCrowdedShoulderEscape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSIdleYieldCrowdedShoulderEscapeTest::RunTest(const FString& Parameters)
{
	TSharedRef<FLSBreachCalibrationState> State = MakeShared<FLSBreachCalibrationState>();
	State->Test = this;
	if (!PrepareIsolatedBreachTemplate(State, FVector(5749.178, 0, 500)))
	{
		if (State->EditorCopy.IsValid())
			FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
		return false;
	}
	FLSCrowdedShoulderEscapeCommand::FBodyPathQuery Witness = [](const ULSOrderComponent* Orders, const TArray<FVector>& Centers)
	{
		return Orders->IsIdleYieldPathClear(Centers);
	};
	FLSCrowdedShoulderEscapeCommand::FOriginHeightQuery OriginHeight = [](const ULSOrderComponent* Orders, double& OutHeight)
	{
		return Orders->GetNavigationOriginHeight(OutHeight);
	};
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSCrowdedShoulderEscapeCommand>(State, MoveTemp(Witness), MoveTemp(OriginHeight)));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSIdleYieldForwardEscapeTest,
	"Landship.Maps.Battlefield.IdleYieldForwardEscape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSIdleYieldForwardEscapeTest::RunTest(const FString& Parameters)
{
	TSharedRef<FLSBreachCalibrationState> State = MakeShared<FLSBreachCalibrationState>();
	State->Test = this;
	if (!PrepareIsolatedBreachTemplate(State, CastleStage))
	{
		if (State->EditorCopy.IsValid())
			FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
		return false;
	}
	FLSCrowdedShoulderEscapeCommand::FBodyPathQuery Witness = [](const ULSOrderComponent* Orders, const TArray<FVector>& Centers)
	{
		return Orders->IsIdleYieldPathClear(Centers);
	};
	FLSCrowdedShoulderEscapeCommand::FOriginHeightQuery OriginHeight = [](const ULSOrderComponent* Orders, double& OutHeight)
	{
		return Orders->GetNavigationOriginHeight(OutHeight);
	};
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSCrowdedShoulderEscapeCommand>(State, MoveTemp(Witness), MoveTemp(OriginHeight), true));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleAutomaticCornerClearanceTest,
	"Landship.Maps.Battlefield.CastleAutomaticCornerClearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleAutomaticCornerClearanceTest::RunTest(const FString& Parameters)
{
	TSharedRef<FLSBreachCalibrationState> State = MakeShared<FLSBreachCalibrationState>();
	State->Test = this;
	if (!PrepareIsolatedBreachTemplate(State, FVector(0, 0, 500)))
	{
		if (State->EditorCopy.IsValid())
			FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSCastleAutomaticCornerCommand>(State));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBreachCalibrationCleanup>(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleAuthoredLegStaticClearanceTest,
	"Landship.Maps.Battlefield.CastleAuthoredLegStaticClearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleAuthoredLegStaticClearanceTest::RunTest(const FString& Parameters)
{
	return QueueCastleNavGeometryState(*this, ELSCastleNavGeometryState::GatesOpen);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleNavIntactGeometryClearanceTest,
	"Landship.Maps.Battlefield.CastleNavIntactGeometryClearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleNavIntactGeometryClearanceTest::RunTest(const FString& Parameters)
{
	return QueueCastleNavGeometryState(*this, ELSCastleNavGeometryState::GatesIntact);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleNavClearedGeometryClearanceTest,
	"Landship.Maps.Battlefield.CastleNavClearedGeometryClearance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleNavClearedGeometryClearanceTest::RunTest(const FString& Parameters)
{
	return QueueCastleNavGeometryState(*this, ELSCastleNavGeometryState::AllDestructibleFixturesRemoved);
}

#endif
