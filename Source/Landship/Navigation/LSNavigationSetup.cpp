#include "Navigation/LSNavigationSetup.h"

#include "NavigationData.h"
#include "NavigationSystem.h"

ALSNavigationSetup::ALSNavigationSetup()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ALSNavigationSetup::BeginPlay()
{
	Super::BeginPlay();
	if (!FMath::IsFinite(AgentRadius) || AgentRadius <= 0 || !FMath::IsFinite(AgentHeight) || AgentHeight <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Navigation setup has invalid agent dimensions: %s"), *GetPathName());
		return;
	}
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	ANavigationData* Data = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create) : nullptr;
	if (!Data)
	{
		UE_LOG(LogTemp, Error, TEXT("Navigation setup requires navigation data and NavMeshBoundsVolume: %s"),
		       *GetPathName());
		return;
	}
	FNavDataConfig Config = Data->GetConfig();
	Config.AgentRadius = AgentRadius;
	Config.AgentHeight = AgentHeight;
	Data->SetConfig(Config);
	Data->RebuildAll();
	UE_LOG(LogTemp, Display, TEXT("LS_NAV_SETUP initialized radius=%.1f height=%.1f owner=%s"), AgentRadius,
	       AgentHeight, *GetPathName());
}
