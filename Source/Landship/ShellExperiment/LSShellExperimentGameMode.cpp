#include "LSShellExperimentGameMode.h"

#include "Combat/LSCombatDeveloperSettings.h"
#include "Controls/LSRTSCameraPawn.h"
#include "GameFramework/HUD.h"
#include "LSShellExperimentController.h"

ALSShellExperimentGameMode::ALSShellExperimentGameMode()
{
	PlayerControllerClass = ALSShellExperimentController::StaticClass();
	DefaultPawnClass = ALSRTSCameraPawn::StaticClass();
	HUDClass = AHUD::StaticClass();
}

void ALSShellExperimentGameMode::StartPlay()
{
	const ULSCombatDeveloperSettings* Settings = GetDefault<ULSCombatDeveloperSettings>();
	bConfigurationValid = Settings->bAllowFriendlyFire && Settings->bAllowSelfDamage;
	ConfigurationError = bConfigurationValid ? FString() :
		TEXT("실험을 시작할 수 없다. Combat Development에서 아군 피해와 자기 피해를 모두 허용해야 한다.");
	// 액터 BeginPlay가 실행되기 전에 실험 실행부가 조회할 설정 결과를 확정한다.
	Super::StartPlay();
}
