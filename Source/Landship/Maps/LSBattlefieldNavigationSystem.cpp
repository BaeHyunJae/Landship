#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Maps/LSBattlefieldCastleNavMesh.h"

#include "NavMesh/RecastNavMesh.h"
#include "UObject/UObjectGlobals.h"

ULSBattlefieldNavigationSystem::ULSBattlefieldNavigationSystem(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FNavDataConfig Troops(142.0f, 284.0f);
	Troops.Name = TEXT("Troops");
	Troops.SetNavDataClass(ARecastNavMesh::StaticClass());
	FNavDataConfig Castle(580.0f, 1000.0f);
	Castle.Name = TEXT("Castle");
	Castle.SetNavDataClass(ALSBattlefieldCastleNavMesh::StaticClass());
	for (FNavDataConfig* Agent : {&Troops, &Castle})
	{
		Agent->DefaultQueryExtent.X = FMath::Max(Agent->DefaultQueryExtent.X, static_cast<double>(Agent->AgentRadius));
		Agent->DefaultQueryExtent.Y = FMath::Max(Agent->DefaultQueryExtent.Y, static_cast<double>(Agent->AgentRadius));
		// 등록 키와 생성 설정에 같은 계단 높이를 사용한다.
		Agent->AgentStepHeight = GetDefault<ARecastNavMesh>()->GetAgentMaxStepHeight(ENavigationDataResolution::Default);
	}
	SupportedAgents = {Troops, Castle};
	DefaultAgentName = Troops.Name;
}
