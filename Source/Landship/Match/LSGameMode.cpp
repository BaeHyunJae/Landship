#include "LSGameMode.h"

#include "Controls/LSPlayerController.h"
#include "LSGameState.h"

ALSGameMode::ALSGameMode()
{
	GameStateClass = ALSGameState::StaticClass();
	PlayerControllerClass = ALSPlayerController::StaticClass();
}
