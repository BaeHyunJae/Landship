#include "LSGameMode.h"

#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Controls/LSRTSHUD.h"
#include "LSGameState.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Troops/LSUnit.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALSGameMode::ALSGameMode()
{
	GameStateClass = ALSGameState::StaticClass();
	PlayerControllerClass = ALSPlayerController::StaticClass();
	DefaultPawnClass = ALSRTSCameraPawn::StaticClass();
	HUDClass = ALSRTSHUD::StaticClass();
}

void ALSGameMode::InitGameState()
{
	Super::InitGameState();
	if (ALSGameState* State = GetGameState<ALSGameState>())
	{
		State->SetPopulationLimit(100);
	}
}

void ALSGameMode::StartPlay()
{
	ALSGameState* State = GetGameState<ALSGameState>();
	const ULSBattlefieldStateSubsystem* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	if (State && Battlefield && Battlefield->IsSetupValid() && !WorldTickEndHandle.IsValid())
	{
		// Subsystem의 참조 해석 뒤, 액터 BeginPlay가 종료 사실을 만들기 전에 등록한다.
		State->bBattleOutcomeEnabled = true;
		WorldTickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::CollectBattleOutcome);
	}
	Super::StartPlay();
}

void ALSGameMode::CollectBattleOutcome(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || World->bIsTearingDown)
	{
		return;
	}
	ALSGameState* State = GetGameState<ALSGameState>();
	const ULSBattlefieldStateSubsystem* Battlefield = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	if (!State || !State->IsBattleOutcomeEnabled() || State->HasBattleEnded() || !Battlefield)
	{
		return;
	}
	FLSOutpostCaptureSnapshot Capture;
	const ELSBattleOutcome Outcome = LSBattleOutcome::Evaluate(
		Battlefield->TryGetCoreCapture(Capture), Battlefield->HasObservedPlayerCastleDeath());
	if (Outcome == ELSBattleOutcome::InProgress)
	{
		return;
	}
	// 정지 중 경로 완료·피해 콜백이 재진입해도 결과를 다시 확정하지 않는다.
	State->BattleOutcome = Outcome;
	const TWeakObjectPtr<ALSGameState> WeakState(State);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	StopBattleActions();
	if (WeakState.IsValid() && WeakWorld.IsValid() && !World->bIsTearingDown)
	{
		WeakState->OnBattleOutcomeFinalizedEvent.Broadcast(Outcome);
	}
}

void ALSGameMode::StopBattleActions()
{
	const TWeakObjectPtr<UWorld> World(GetWorld());
	// 정지 콜백이 액터를 제거해도 순회 대상을 다시 역참조하지 않도록 스냅샷을 사용한다.
	TArray<TWeakObjectPtr<AActor>> Actors;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		Actors.Add(*It);
	}
	for (const TWeakObjectPtr<AActor>& WeakActor : Actors)
	{
		if (!World.IsValid() || World->bIsTearingDown) return;
		AActor* Actor = WeakActor.Get();
		if (!Actor || Actor->IsActorBeingDestroyed()) continue;
		if (ALSPlayerController* Controller = Cast<ALSPlayerController>(Actor)) Controller->EndBattleActions();
		if (ALSUnit* Unit = Cast<ALSUnit>(WeakActor.Get())) Unit->EndBattleActions();
		if (ALSCastle* Castle = Cast<ALSCastle>(WeakActor.Get())) Castle->EndBattleActions();
		Actor = WeakActor.Get();
		if (!Actor || Actor->IsActorBeingDestroyed()) continue;
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component) || !WeakActor.IsValid()) continue;
			if (ULSOrderComponent* Orders = Cast<ULSOrderComponent>(Component)) Orders->EndBattleActions();
			else if (ULSEnemyBehaviorComponent* Behavior = Cast<ULSEnemyBehaviorComponent>(Component)) Behavior->EndBattleActions();
			else if (ULSBasicAttackComponent* Attack = Cast<ULSBasicAttackComponent>(Component)) Attack->EndBattleActions();
			else if (ULSCastleWeaponComponent* Weapons = Cast<ULSCastleWeaponComponent>(Component)) Weapons->EndBattleActions();
		}
	}
}

void ALSGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FWorldDelegates::OnWorldTickEnd.Remove(WorldTickEndHandle);
	WorldTickEndHandle.Reset();
	Super::EndPlay(EndPlayReason);
}
