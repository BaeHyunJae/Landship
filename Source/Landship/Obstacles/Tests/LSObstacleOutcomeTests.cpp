#if WITH_DEV_AUTOMATION_TESTS
#include "Obstacles/LSMine.h"
#include "Obstacles/LSTurret.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"
#include "Match/Tests/LSBattleOutcomeTestObserver.h"
#include "Troops/LSTroopUnit.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
template<class T> T* SpawnObstacleOutcome(UWorld* World, const FVector& Location)
{
 FActorSpawnParameters Parameters;
 Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Parameters);
}
FLSHealthSnapshot ObstacleOutcomeHealth(AActor* Actor)
{
 FLSHealthSnapshot Snapshot;
 if (IsValid(Actor))
  if (const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>()) Health->TryGetHealth(Snapshot);
 return Snapshot;
}
void TickObstacleOutcome(UWorld* World)
{
 ++GFrameCounter;
 World->Tick(LEVELTICK_All, .01f);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleOutcomeIntegrationTest,
 "Landship.Obstacles.Outcome.TurretTerminationAndRemainingChain",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSObstacleOutcomeIntegrationTest::RunTest(const FString& Parameters)
{
 FTestWorldWrapper Play;
 if (!Play.CreateTestWorld(EWorldType::Game)) return false;
 UWorld* World = Play.GetTestWorld();
 World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
 ALSBattlefieldSetup* Setup = SpawnObstacleOutcome<ALSBattlefieldSetup>(World, FVector::ZeroVector);
 ALSMobileCastle* Castle = SpawnObstacleOutcome<ALSMobileCastle>(World, FVector(10000, 0, 142));
 ALSEnemyOutpost* Core = SpawnObstacleOutcome<ALSEnemyOutpost>(World, FVector(0, 0, 100));
 ALSTroopUnit* Target = SpawnObstacleOutcome<ALSTroopUnit>(World, FVector(20000, 0, 88));
 ALSTurret* Existing = SpawnObstacleOutcome<ALSTurret>(World, FVector(20500, 0, 120));
 ALSMine* First = SpawnObstacleOutcome<ALSMine>(World, FVector(11000, 0, 0));
 ALSMine* Second = SpawnObstacleOutcome<ALSMine>(World, FVector(10350, 0, 0));
 if (!Setup || !Castle || !Core || !Target || !Existing || !First || !Second) return false;
 Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
 Castle->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
 Core->BasicAttack->SetAutoAttackOnBeginPlay(false);
 if (!Play.BeginPlayInTestWorld()) return false;
 ALSGameState* State = World->GetGameState<ALSGameState>();
 if (!TestNotNull(TEXT("Real outcome GameState"), State) ||
  !TestTrue(TEXT("Explicit battlefield setup enables real authority"), State->IsBattleOutcomeEnabled())) return false;
 Core->Behavior->SetComponentTickEnabled(false);
 Castle->GetCharacterMovement()->DisableMovement(); Target->GetCharacterMovement()->DisableMovement();
 ULSBasicAttackComponent* ExistingAttack = Existing->FindComponentByClass<ULSBasicAttackComponent>();
 if (!TestNotNull(TEXT("Existing real turret attack component"), ExistingAttack)) return false;
 TestTrue(TEXT("Existing turret can attack before outcome"), ExistingAttack->StartAttack(Target));
 ULSBattleOutcomeTestObserver* Observer = NewObject<ULSBattleOutcomeTestObserver>(State);
 State->OnBattleOutcomeFinalizedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnFinalized);
 Observer->AttackToInspect = ExistingAttack;
 Castle->FindComponentByClass<ULSHealthComponent>()->OnDiedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnCastleDied);
 const FLSHealthSnapshot CastleHealth = ObstacleOutcomeHealth(Castle);
 Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
  ULSHealthComponent::MakeDamageRequest(CastleHealth.MaxHealth * .7f, nullptr, nullptr));
 TestTrue(TEXT("First mine has a real pending chain timer before outcome"), First->RequestTrigger(ELSMineTriggerCause::Chain));
 TestTrue(TEXT("Second mine remains armed before chain reaches it"), Second->IsArmed());
 const TWeakObjectPtr<ALSMine> WeakFirst(First), WeakSecond(Second);
 Core->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
  ULSHealthComponent::MakeDamageRequest(ObstacleOutcomeHealth(Core).CurrentHealth, nullptr, nullptr));
 TickObstacleOutcome(World);
 TestEqual(TEXT("Core capture finalizes actual victory before pending blast"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
 TestTrue(TEXT("Result finalization observed existing attack stopped"), Observer->bSawStoppedAttackAtFinalization);
 TestFalse(TEXT("Existing turret direct attack is terminal after outcome"), ExistingAttack->StartAttack(Target));
 ExistingAttack->Activate(true);
 TestFalse(TEXT("Reactivation cannot restart existing turret automatic attack"), ExistingAttack->StartAutoAttack());
 ALSTurret* Late = SpawnObstacleOutcome<ALSTurret>(World, FVector(20500, 300, 120));
 if (!TestNotNull(TEXT("Late turret spawned after actual result"), Late)) return false;
 ULSBasicAttackComponent* LateAttack = Late->FindComponentByClass<ULSBasicAttackComponent>();
 if (!TestNotNull(TEXT("Late turret attack component"), LateAttack)) return false;
 LateAttack->OnAttackExecutedEvent.AddDynamic(Observer, &ULSBattleOutcomeTestObserver::OnExecuted);
 TestFalse(TEXT("Late turret cannot start direct attack"), LateAttack->StartAttack(Target));
 LateAttack->Activate(true);
 TestFalse(TEXT("Late turret cannot restart automatic attack after activation"), LateAttack->StartAutoAttack());
 // 종료 전에 발사한 탄은 허용되므로 도착 시간을 지난 뒤 새 피해의 기준을 잡는다.
 bool bSawSecondTriggered = false;
 for (int32 Index = 0; Index < 100; ++Index)
 {
  TickObstacleOutcome(World);
  bSawSecondTriggered |= WeakSecond.IsValid() && WeakSecond->IsTriggered();
 }
 const float TargetHealthAfterRemainingShots = ObstacleOutcomeHealth(Target).CurrentHealth;
 for (int32 Index = 0; Index < 350; ++Index) TickObstacleOutcome(World);
 TestTrue(TEXT("Pending blast actually triggers next mine after finalization"), bSawSecondTriggered);
 TestFalse(TEXT("First chain mine finishes and is removed"), WeakFirst.IsValid());
 TestFalse(TEXT("Second chain mine finishes and is removed"), WeakSecond.IsValid());
 TestTrue(TEXT("Remaining second blast actually kills low-health player castle"), ObstacleOutcomeHealth(Castle).bIsDead);
 TestEqual(TEXT("Actual castle death notification observed once after victory"), Observer->PublicCastleDeaths, 1);
 TestEqual(TEXT("Remaining blast cannot rewrite final victory"), State->GetBattleOutcome(), ELSBattleOutcome::Victory);
 TestEqual(TEXT("Actual result finalizes exactly once"), Observer->Outcomes.Num(), 1);
 TestEqual(TEXT("Late turret produces no actual attack executions"), Observer->ExecutionSources.Num(), 0);
 TestEqual(TEXT("Stopped and late turrets cause no new damage after old shots settle"), ObstacleOutcomeHealth(Target).CurrentHealth, TargetHealthAfterRemainingShots);
 ALSMine* LateMine = SpawnObstacleOutcome<ALSMine>(World, FVector(22000, 0, 0));
 if (!TestNotNull(TEXT("Late mine exists after outcome"), LateMine)) return false;
 TestFalse(TEXT("Public late disarm claim rejects after actual outcome"), LateMine->TryClaimDisarm(Target));
 TestFalse(TEXT("Public late disarm completion rejects after actual outcome"), LateMine->TryCompleteDisarm(Target));
 Play.ForwardErrorMessages(this);
 return true;
}
#endif

