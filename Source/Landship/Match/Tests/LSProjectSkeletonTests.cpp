#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "Combat/LSCollisionChannels.h"
#include "Controls/LSPlayerController.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSGameStatePopulationTest, "Landship.Skeleton.Population",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSGameStatePopulationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	ALSGameState* State = World->SpawnActor<ALSGameState>();
	if (TestNotNull(TEXT("게임 상태 생성"), State))
	{
		TestTrue(TEXT("초기 상태 지정"), State->SetPopulation(2, 5));
		TestTrue(TEXT("인구 증가"), State->ChangePopulation(3));
		TestEqual(TEXT("현재 인구 조회"), State->GetCurrentPopulation(), 5);
		TestFalse(TEXT("상한 초과 거부"), State->ChangePopulation(1));
		TestFalse(TEXT("현재 인구 미만 상한 거부"), State->SetPopulationLimit(4));
		TestEqual(TEXT("실패 시 기존 상한 유지"), State->GetPopulationLimit(), 5);
		TestFalse(TEXT("음수 상한 거부"), State->SetPopulation(0, -1));
		TestFalse(TEXT("인구 음수 거부"), State->ChangePopulation(-6));
		TestTrue(TEXT("인구 감소"), State->ChangePopulation(-2));
		TestEqual(TEXT("감소 결과"), State->GetCurrentPopulation(), 3);
		TestTrue(TEXT("큰 정수 상태 지정"), State->SetPopulation(MAX_int32 - 1, MAX_int32));
		TestFalse(TEXT("덧셈 오버플로 거부"), State->ChangePopulation(MAX_int32));
		TestEqual(TEXT("오버플로 시 상태 유지"), State->GetCurrentPopulation(), MAX_int32 - 1);
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectConfigurationTest, "Landship.Skeleton.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectConfigurationTest::RunTest(const FString& Parameters)
{
	const ALSGameMode* Mode = GetDefault<ALSGameMode>();
	TestTrue(TEXT("기본 게임 상태 연결"), Mode->GameStateClass == ALSGameState::StaticClass());
	TestTrue(TEXT("기본 컨트롤러 연결"), Mode->PlayerControllerClass == ALSPlayerController::StaticClass());
	const UCollisionProfile* Profile = UCollisionProfile::Get();
	TestEqual(TEXT("유닛 채널"), Profile->ReturnChannelNameFromContainerIndex(LSCollisionChannels::Unit), FName(TEXT("LSUnit")));
	TestEqual(TEXT("건물 채널"), Profile->ReturnChannelNameFromContainerIndex(LSCollisionChannels::Building), FName(TEXT("LSBuilding")));
	TestEqual(TEXT("경로 장애물 채널"), Profile->ReturnChannelNameFromContainerIndex(LSCollisionChannels::PathObstacle), FName(TEXT("LSPathObstacle")));
	TestEqual(TEXT("투사체 채널"), Profile->ReturnChannelNameFromContainerIndex(LSCollisionChannels::Projectile), FName(TEXT("LSProjectile")));
	TestEqual(TEXT("지면 채널"), Profile->ReturnChannelNameFromContainerIndex(LSCollisionChannels::Ground), FName(TEXT("LSGround")));
	return true;
}

#endif
