#include "LSGameMode.h"

#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Controls/LSRTSHUD.h"
#include "LSGameState.h"

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
