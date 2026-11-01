#include "Controls/LSCommandProviderComponent.h"

TArray<FLSCommandEntry> ULSCommandProviderComponent::GetCommands_Implementation(ALSPlayerController*) const
{
	return {};
}
bool ULSCommandProviderComponent::ExecuteCommand_Implementation(FName, ALSPlayerController*)
{
	return false;
}
