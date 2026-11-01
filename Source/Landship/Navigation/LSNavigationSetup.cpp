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
	// 경로 모서리와 도착 허용 반경 안에서 정지한 큰 에이전트도 다음 경로의 출발점을 찾을 수 있어야 한다.
	// 기본 50cm 검색 범위는 반경 142cm인 성이 내비게이션 경계 옆에서 멈춘 경우를 포착하지 못한다.
	Config.DefaultQueryExtent.X = FMath::Max(Config.DefaultQueryExtent.X, static_cast<double>(AgentRadius));
	Config.DefaultQueryExtent.Y = FMath::Max(Config.DefaultQueryExtent.Y, static_cast<double>(AgentRadius));
	Data->SetConfig(Config);
	Data->RebuildAll();
	UE_LOG(LogTemp, Display, TEXT("LS_NAV_SETUP initialized radius=%.1f height=%.1f queryExtent=%s owner=%s"),
	       AgentRadius, AgentHeight, *Config.DefaultQueryExtent.ToString(), *GetPathName());
}
