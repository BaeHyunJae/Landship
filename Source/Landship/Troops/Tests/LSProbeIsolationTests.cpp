#if WITH_DEV_AUTOMATION_TESTS

#include "Troops/Tests/LSHMHTroopProbe.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProbeIsolationTest, "Landship.Navigation.ProbeIsolation",
                                EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProbeIsolationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	ALSHMHTroopProbe* Probe = TestWorld.GetTestWorld()->SpawnActor<ALSHMHTroopProbe>();
	if (!TestNotNull(TEXT("검증 액터 생성"), Probe))
	{
		return false;
	}
	TestTrue(TEXT("검증 액터는 쿠킹 시 제외 대상"), Probe->IsEditorOnly());
	if (!TestWorld.BeginPlayInTestWorld())
	{
		return false;
	}
	TestTrue(TEXT("일반 실행에서는 검증 액터 제거"), !IsValid(Probe) || Probe->IsActorBeingDestroyed());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
