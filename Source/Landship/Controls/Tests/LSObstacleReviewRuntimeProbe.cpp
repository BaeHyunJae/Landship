#include "Controls/Tests/LSObstacleReviewRuntimeProbe.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSPlayerController.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSBarricade.h"
#include "Navigation/LSMineAvoidance.h"
#include "Navigation/LSMineAwareMovementComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
namespace
{
FLSHealthSnapshot ReviewHealth(const AActor* Actor)
{
 FLSHealthSnapshot Snapshot;
 if (IsValid(Actor))
  if (const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>()) Health->TryGetHealth(Snapshot);
 return Snapshot;
}
}
ALSObstacleReviewRuntimeProbe::ALSObstacleReviewRuntimeProbe()
{
 PrimaryActorTick.bCanEverTick = true;
 PrimaryActorTick.TickInterval = .02f;
}
void ALSObstacleReviewRuntimeProbe::BeginPlay()
{
 Super::BeginPlay();
 if (!FParse::Param(FCommandLine::Get(), TEXT("ObstacleReviewRuntimeTestExit")))
 { SetActorTickEnabled(false); Destroy(); }
}
template<class T> T* ALSObstacleReviewRuntimeProbe::Spawn(const FVector& Position)
{
 FActorSpawnParameters Params;
 Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 T* Actor = GetWorld()->SpawnActor<T>(Position, FRotator::ZeroRotator, Params);
 if (Actor) { Owned.Add(Actor); }
 return Actor;
}
ALSTroopUnit* ALSObstacleReviewRuntimeProbe::SpawnTroop(const FVector& Position, bool bEnemy)
{
 const FTransform Transform(FRotator::ZeroRotator, Position);
 ALSTroopUnit* Unit = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform,
  nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!Unit) { return nullptr; }
 Unit->SetInitialFaction(bEnemy ? ELSFaction::Enemy : ELSFaction::Ally);
 Unit->SetInitialTroopType(ELSTroopType::Spearman);
 Unit->FinishSpawning(Transform);
 Owned.Add(Unit);
 if (Unit->GetController()) { Owned.Add(Unit->GetController()); }
 Unit->Vision->SetSightRadius(5000);
 Unit->Orders->SetComponentTickEnabled(false);
 Unit->BasicAttack->StopAttack();
 return Unit;
}
void ALSObstacleReviewRuntimeProbe::OnMineDestroyed(AActor* Actor)
{
 if (const ALSMine* Mine = Cast<ALSMine>(Actor)) { LastMineState = static_cast<int32>(Mine->GetMineState()); }
}
void ALSObstacleReviewRuntimeProbe::MakeRing()
{
 for (int32 Index = 0; Index < 6; ++Index)
 {
  const double Angle = 2 * PI * Index / 6;
  ALSMine* Mine = Spawn<ALSMine>(FVector(-600 + 260 * FMath::Cos(Angle), -1800 + 260 * FMath::Sin(Angle), 0));
  Mines.Add(Mine);
  if (Index == 0) { WorkMine = Mine; }
 }
}
void ALSObstacleReviewRuntimeProbe::Check(bool bPass, const TCHAR* Name)
{
 Failures += bPass ? 0 : 1;
 UE_LOG(LogTemp, Display, TEXT("OBSTACLE_REVIEW_RUNTIME %s: %s phase=%d t=%.2f"), bPass ? TEXT("PASS") : TEXT("FAIL"), Name, Phase, PhaseTime);
}
void ALSObstacleReviewRuntimeProbe::Cleanup()
{
 if (ALSPlayerController* Player = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController())) { Player->ApplySelection({}); }
 for (const auto& Actor : Owned) { if (Actor.IsValid()) { Actor->Destroy(); } }
 Owned.Reset(); Mines.Reset(); Source.Reset(); Enemy.Reset(); Threat.Reset(); Castle.Reset(); Barrier.Reset(); WorkMine.Reset();
}
void ALSObstacleReviewRuntimeProbe::Next(int32 NewPhase)
{
 Cleanup(); Phase = NewPhase; PhaseTime = 0; EventTime = -1; NextDiagnostic = 0;
 bInitialized = bIssued = bObservedBlocked = bObservedAvoidance = false; LastMineState = -1;
}
void ALSObstacleReviewRuntimeProbe::Finish()
{
 if (bFinished) { return; }
 bFinished = true; Cleanup(); SetActorTickEnabled(false);
 UE_LOG(LogTemp, Display, TEXT("OBSTACLE_REVIEW_RUNTIME_FINISHED failures=%d elapsed=%.2f"), Failures, TotalTime);
 FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
void ALSObstacleReviewRuntimeProbe::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (bFinished) { return; }
 PhaseTime += DeltaSeconds; TotalTime += DeltaSeconds;
 if (TotalTime > 100) { Check(false, TEXT("bounded total timeout")); Finish(); return; }
 if (Phase == 0)
 {
  if (PhaseTime < 2 || (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) && PhaseTime < 12)) { return; }
  UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Point;
  Check(Nav && Nav->ProjectPointToNavigation(FVector(-600, -1800, 0), Point), TEXT("review fixture real navigation ready"));
  if (!Nav) { Finish(); return; }
  Next(1); return;
 }
 if (!bInitialized)
 {
  bInitialized = true;
  SourceStart = FVector(-600, -1800, 88);
  Source = SpawnTroop(SourceStart, false);
  if (!Source.IsValid()) { Check(false, TEXT("review source native troop spawn")); Finish(); return; }
  if (Phase == 1)
  {
   Source->SetActorLocation(FVector(-800, -1800, 88));
   Barrier = Spawn<ALSBarricade>(FVector(0, -1800, 90));
   for (float Offset : {-650.f, 650.f})
   {
    AStaticMeshActor* Wall = Spawn<AStaticMeshActor>(FVector(0, -1800 + Offset, 150));
    if (!Wall) { Check(false, TEXT("wide face corridor wall spawn")); Finish(); return; }
    auto* Mesh = Wall->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCollisionObjectType(LSCollisionChannels::Building);
    Mesh->SetCanEverAffectNavigation(true); Wall->SetActorScale3D(FVector(80, 8, 3));
   }
   FLSBasicAttackSettings Settings = Source->BasicAttack->GetAttackSettings(); Settings.Damage = 1100;
   Source->BasicAttack->ConfigureAttack(Settings);
  }
  else if (Phase <= 4)
  {
   MakeRing(); Enemy = SpawnTroop(FVector(0, -1800, 88), true);
   OriginalDestination = FVector(-700, -1800, 0);
  }
  else
  {
   WorkMine = Spawn<ALSMine>(FVector(0, -1800, 0));
   Castle = Spawn<ALSMobileCastle>(FVector(-350, -1500, 142));
   Source->SetActorLocation(FVector(-800, -1800, 88));
   if (WorkMine.IsValid()) { WorkMine->OnDestroyed.AddDynamic(this, &ThisClass::OnMineDestroyed); }
   if (Phase == 6)
   {
    Source->SetActorLocation(FVector(-1400, -1800, 88));
    if (Castle.IsValid()) { Castle->SetActorLocation(FVector(-600, -1800, 142)); }
   }
   if (Castle.IsValid())
   {
    Castle->Orders->SetComponentTickEnabled(false); Castle->Vision->SetSightRadius(5000);
    if (Castle->GetController()) { Owned.Add(Castle->GetController()); }
   }
  }
  Check(Source.IsValid() && (Phase == 1 ? Barrier.IsValid() : WorkMine.IsValid()), TEXT("review phase native actors available"));
  if (!Source.IsValid() || (Phase == 1 ? !Barrier.IsValid() : !WorkMine.IsValid())) { Finish(); return; }
  GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
  return;
 }
 if (PhaseTime >= NextDiagnostic)
 {
  NextDiagnostic = PhaseTime + 1;
  const auto* Movement = Source.IsValid() ? Cast<ULSMineAwareMovementComponent>(Source->GetCharacterMovement()) : nullptr;
  UE_LOG(LogTemp, Display, TEXT("OBSTACLE_REVIEW_RUNTIME_SAMPLE phase=%d t=%.2f pos=%s speed=%.2f order=%d engaged=%s attack=%s avoidance=%d enemyHP=%.2f threatHP=%.2f event=%.2f"),
   Phase, PhaseTime, Source.IsValid() ? *Source->GetActorLocation().ToCompactString() : TEXT("invalid"),
   Source.IsValid() ? Source->GetVelocity().Size2D() : 0, Source.IsValid() ? static_cast<int32>(Source->Orders->GetOrderKind()) : -1,
   Source.IsValid() ? *GetNameSafe(Source->Orders->GetEngagedTarget()) : TEXT("invalid"),
   Source.IsValid() ? *GetNameSafe(Source->BasicAttack->GetAttackTarget()) : TEXT("invalid"), Movement && Movement->IsMineAvoidanceEnabled(),
   ReviewHealth(Enemy.Get()).CurrentHealth, ReviewHealth(Threat.Get()).CurrentHealth, EventTime);
 }
 if (!bIssued)
 {
  if (PhaseTime < (Phase == 1 ? 2.f : .4f) || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) { return; }
  if (!Source.IsValid()) { Check(false, TEXT("review source native troop spawn")); Finish(); return; }
  bIssued = true; Source->Orders->SetComponentTickEnabled(true);
  FLSOrder Order;
  if (Phase == 1)
  {
   Order.Kind = ELSOrderKind::AttackTarget; Order.Target = Barrier.Get();
   Check(Source->Orders->SubmitOrder(Order), TEXT("F1 public AttackTarget accepts wide barricade face in fully spanning corridor"));
  }
  else if (Phase == 2)
  {
   Order.Kind = ELSOrderKind::Disarm; Order.Target = WorkMine.Get();
   Check(Source->Orders->SubmitOrder(Order), TEXT("F2 real Disarm begins before direct attack replacement"));
  }
  else if (Phase <= 4)
  {
   Order.Kind = ELSOrderKind::AttackMove; Order.Destination = OriginalDestination;
   Check(Source->Orders->SubmitOrder(Order), TEXT("F3 public AttackMove accepts original destination inside closed ring"));
   OriginalDestination = Source->Orders->GetOrderDestination();
  }
  else if (Phase == 5)
  {
   ALSPlayerController* Player = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
   if (!Player || !Castle.IsValid()) { Check(false, TEXT("F4 real player controller and castle available")); Finish(); return; }
   FLSOrder Hold; Hold.Kind = ELSOrderKind::Hold;
   Castle->Orders->SubmitOrder(Hold); Player->ApplySelection({Castle.Get()});
   Check(!Player->IssueSelectionOrder(ELSOrderKind::Disarm, FVector::ZeroVector, WorkMine.Get(), false) && Castle->Orders->GetOrderKind() == ELSOrderKind::Hold && !WorkMine->HasDisarmWorker(),
    TEXT("F4 castle-only Disarm rejects without replacing existing Hold"));
   Player->ApplySelection({Castle.Get(), Source.Get()});
   Check(Player->IssueSelectionOrder(ELSOrderKind::Disarm, FVector::ZeroVector, WorkMine.Get(), false), TEXT("F4 mixed closer castle and farther troop public Disarm accepted"));
   Check(Source->Orders->GetOrderKind() == ELSOrderKind::Disarm && Castle->Orders->GetOrderKind() == ELSOrderKind::Hold && WorkMine->HasDisarmWorker(),
    TEXT("F4 farther troop claims mine and closer castle Holds"));
   EventTime = PhaseTime;
  }
  else
  {
   ALSPlayerController* Player = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
   const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
   if (!Player || !Castle.IsValid() || !Settings || !WorkMine.IsValid())
   { Check(false, TEXT("F7b real minimap weapon prerequisites")); Finish(); return; }
   Player->ApplySelection({Castle.Get()});
   const bool bArmed = Player->ArmCastleWeapon(ELSCastleWeapon::Shotgun);
   Check(bArmed, TEXT("F7b real project Shotgun definition arms on selected castle"));
   Player->AimedObstacle = WorkMine.Get();
   const FVector MinimapAim(-600, -2200, Settings->GetGroundHeight());
   Check(Player->HandleMinimapInput(MinimapAim, false, false), TEXT("F7b actual minimap weapon dispatch accepted"));
   ELSCastleWeapon Weapon = ELSCastleWeapon::Shotgun; FVector ActualAim = FVector::ZeroVector;
   const bool bCast = Castle->Weapons->GetRecentCast(Weapon, ActualAim);
   Check(bArmed && Player->GetLastWeaponResult() == ELSWeaponCastResult::Ready && bCast && Weapon == ELSCastleWeapon::Shotgun && ActualAim.Equals(MinimapAim, .1) && !ActualAim.Equals(WorkMine->GetInteractionLocation(), .1),
    TEXT("F7b recent real Shotgun cast uses minimap ground despite stale visible mine hover cache"));
   UE_LOG(LogTemp, Display, TEXT("OBSTACLE_REVIEW_MINIMAP actual=%s expected=%s result=%d"), *ActualAim.ToCompactString(), *MinimapAim.ToCompactString(), static_cast<int32>(Player->GetLastWeaponResult()));
   Finish(); return;
  }
  return;
 }
 if (Phase == 1)
 {
  if ((!Barrier.IsValid() || !Barrier->IsObstacleActive()) && EventTime < 0)
  {
   Check(Source.IsValid() && !ReviewHealth(Source.Get()).bIsDead, TEXT("F1 real troop attack destroyed barricade without direct damage bypass"));
   EventTime = PhaseTime;
  }
  if (EventTime >= 0 && !bObservedBlocked && PhaseTime - EventTime > 1 && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
  {
   FLSOrder Cross; Cross.Kind = ELSOrderKind::Move; Cross.Destination = FVector(800, -1800, 0);
   Check(Source->Orders->SubmitOrder(Cross), TEXT("F1 public movement through attack-destroyed corridor accepted"));
   bObservedBlocked = true;
  }
  if (bObservedBlocked && Source.IsValid() && Source->GetActorLocation().X > 600)
  {
   Check(true, TEXT("F1 troop physically crosses corridor after its actual AttackTarget destroys barricade"));
   Next(2); return;
  }
 }
 else if (Phase == 2)
 {
  if (EventTime < 0 && PhaseTime > 1)
  {
   const auto* Movement = Cast<ULSMineAwareMovementComponent>(Source->GetCharacterMovement());
   Check(Source->Orders->GetOrderKind() == ELSOrderKind::Disarm && WorkMine.IsValid() && WorkMine->HasDisarmWorker() && Movement && Movement->IsMineAvoidanceEnabled(),
    TEXT("F2 replacement begins from active mine avoidance and claimed Disarm"));
   FLSOrder Attack; Attack.Kind = ELSOrderKind::AttackTarget; Attack.Target = Enemy.Get();
   Check(Source->Orders->SubmitOrder(Attack, true) && Source->Orders->GetQueuedCount() == 0 && Source->Orders->GetOrderKind() == ELSOrderKind::AttackTarget && !WorkMine->HasDisarmWorker(),
    TEXT("F2 Shift direct AttackTarget uses incoming direct policy and immediately replaces Disarm"));
   FLSOrder ResumeDisarm; ResumeDisarm.Kind = ELSOrderKind::Disarm; ResumeDisarm.Target = WorkMine.Get();
   Check(Source->Orders->SubmitOrder(ResumeDisarm) && WorkMine->HasDisarmWorker(), TEXT("F2 fresh real Disarm reclaims mine before ordinary replacement"));
   Check(Source->Orders->SubmitOrder(Attack) && Source->Orders->GetOrderKind() == ELSOrderKind::AttackTarget && Source->Orders->GetQueuedCount() == 0 && !WorkMine->HasDisarmWorker(),
    TEXT("F2 immediate direct AttackTarget replaces Disarm clears queue and releases claim"));
   EventTime = PhaseTime;
  }
  if (EventTime >= 0 && (!Source.IsValid() || ReviewHealth(Source.Get()).bIsDead))
  {
   bool bMineSpent = false;
   for (const auto& Mine : Mines) { bMineSpent |= !Mine.IsValid() || Mine->GetMineState() == ELSMineState::Spent; }
   Check(bMineSpent, TEXT("F2 real incoming direct attack crosses ring mine and dies after Disarm replacement"));
   Next(3); return;
  }
 }
 else if (Phase == 3 || Phase == 4)
 {
  if (!Source.IsValid() || ReviewHealth(Source.Get()).bIsDead) { Check(false, TEXT("F3 source remains alive outside mines")); Finish(); return; }
  const auto* Movement = Cast<ULSMineAwareMovementComponent>(Source->GetCharacterMovement());
  bObservedAvoidance |= Movement && Movement->IsMineAvoidanceEnabled();
  if (EventTime < 0 && PhaseTime > 1 && Source->Orders->GetEngagedTarget() == Enemy.Get() && Source->GetVelocity().Size2D() < 1)
  {
   bObservedBlocked = true;
   Check(bObservedAvoidance && Source->Orders->GetOrderKind() == ELSOrderKind::AttackMove && Source->Orders->GetOrderDestination().Equals(OriginalDestination, 1), TEXT("F3 real automatic chase blocked while original AttackMove remains"));
   if (Phase == 3)
   {
    FLSOrder QueuedAttack; QueuedAttack.Kind = ELSOrderKind::AttackTarget; QueuedAttack.Target = Enemy.Get();
    Check(Source->Orders->SubmitOrder(QueuedAttack, true) && Source->Orders->GetQueuedCount() == 1 && Source->Orders->GetOrderKind() == ELSOrderKind::AttackMove,
     TEXT("F2 queued direct AttackTarget accepts incoming direct policy during blocked automatic chase"));
    Enemy->SetActorLocation(FVector(1600, -1800, 88));
    GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
    SourceStart = Source->GetActorLocation();
   }
   else
   {
    Threat = SpawnTroop(Source->GetActorLocation() + FVector(0, -100, 0), true);
    if (!Threat.IsValid()) { Check(false, TEXT("F3 nearby threat spawn")); Finish(); return; }
    ThreatInitialHP = ReviewHealth(Threat.Get()).CurrentHealth;
    FLSBasicAttackSettings Settings = Threat->BasicAttack->GetAttackSettings(); Settings.Damage = 1;
    Threat->BasicAttack->ConfigureAttack(Settings);
    GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
    Check(Threat->BasicAttack->StartAttack(Source.Get()), TEXT("F3 nearby enemy actually begins attacking blocked source"));
    SourceStart = Source->GetActorLocation();
   }
   EventTime = PhaseTime;
  }
  if (EventTime >= 0 && Phase == 3 && Source->Orders->GetEngagedTarget() != Enemy.Get() && FVector::Dist2D(Source->GetActorLocation(), SourceStart) > 10)
  {
   Check(bObservedBlocked && Source->Orders->GetOrderKind() != ELSOrderKind::AttackTarget && ReviewHealth(Source.Get()).CurrentHealth > 0,
    TEXT("F3 target leaves detection and source actually resumes original destination"));
   Next(4); return;
  }
  if (EventTime >= 0 && Phase == 4 && Threat.IsValid() && ReviewHealth(Threat.Get()).CurrentHealth < ThreatInitialHP)
  {
   Check(Source->BasicAttack->GetAttackTarget() == Threat.Get() && FVector::Dist2D(Source->GetActorLocation(), SourceStart) < 5 && Source->Orders->GetOrderKind() == ELSOrderKind::AttackMove && Source->Orders->GetOrderDestination().Equals(OriginalDestination, 1) && Source->Orders->GetEngagedTarget() == Enemy.Get(),
    TEXT("F3 blocked source actually retaliates in place while preserving original blocked intent"));
   Next(5); return;
  }
 }
 else if (Phase == 5)
 {
  if (!WorkMine.IsValid())
  {
   Check(LastMineState == static_cast<int32>(ELSMineState::Disarmed) && PhaseTime - EventTime >= 6 && Source.IsValid() && !ReviewHealth(Source.Get()).bIsDead && Source->Orders->GetOrderKind() == ELSOrderKind::Hold && Castle.IsValid() && Castle->Orders->GetOrderKind() == ELSOrderKind::Hold,
    TEXT("F4 selected farther troop actually completes six-second Disarm while castle Holds"));
   Next(6); return;
  }
 }
 if (PhaseTime > 18) { Check(false, TEXT("bounded review phase required behavior timeout")); Finish(); }
}



