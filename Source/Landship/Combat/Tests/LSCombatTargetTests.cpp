#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "Castle/LSCastle.h"
#include "Enemies/LSOutpost.h"
#include "Troops/LSUnit.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/PrimitiveComponent.h"

namespace
{
template <typename ActorT, typename RowT>
bool CheckBlueprintTarget(FAutomationTestBase& Test, const TCHAR* BlueprintPath, const TCHAR* TablePath,
	ECollisionChannel Channel)
{
	UClass* TargetClass = LoadClass<ActorT>(nullptr, BlueprintPath);
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TablePath);
	if (!Test.TestNotNull(TEXT("확인용 Blueprint"), TargetClass)
		|| !Test.TestNotNull(TEXT("확인용 DataTable"), Table))
	{
		return false;
	}
	const RowT* Expected = Table->FindRow<RowT>(TEXT("PrototypeTest"), TEXT("CombatTargetAssets"));
	if (!Test.TestNotNull(TEXT("확인용 행"), Expected))
	{
		return false;
	}

	FTestWorldWrapper TestWorld;
	if (!Test.TestTrue(TEXT("게임 월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActorT* Target = TestWorld.GetTestWorld()->SpawnActor<ActorT>(TargetClass, FVector(0, 0, 100), FRotator::ZeroRotator, SpawnParameters);
	if (!Test.TestNotNull(TEXT("Blueprint 대상 생성"), Target))
	{
		return false;
	}
	RowT Actual;
	Actual.Health = 4242.0f;
	Test.TestEqual(TEXT("시작 전 미시도 상태"), Target->GetStatsInitResult(), ELSCombatStatsInitResult::NotAttempted);
	Test.TestFalse(TEXT("시작 전 조회 거부"), Target->TryGetStats(Actual));
	Test.TestEqual(TEXT("조회 실패 시 출력 유지"), Actual.Health, 4242.0f);
	const UPrimitiveComponent* Collision = Cast<UPrimitiveComponent>(Target->GetRootComponent());
	if (Test.TestNotNull(TEXT("대상 충돌 루트"), Collision))
	{
		Test.TestEqual(TEXT("전투 대상 채널"), Collision->GetCollisionObjectType(), Channel);
	}

	bool bObservedPawnBeginPlay = false;
	bool bReadyDuringPawnBeginPlay = false;
	FDelegateHandle ReadinessProbe;
	if constexpr (TIsDerivedFrom<ActorT, APawn>::Value)
	{
		ReadinessProbe = APawn::OnPawnBeginPlay.AddLambda([&](APawn* Pawn)
		{
			if (Pawn == Target)
			{
				bObservedPawnBeginPlay = true;
				bReadyDuringPawnBeginPlay = Target->IsStatsInitialized();
			}
		});
	}
	const bool bBeganPlay = TestWorld.BeginPlayInTestWorld();
	APawn::OnPawnBeginPlay.Remove(ReadinessProbe);
	if (!Test.TestTrue(TEXT("게임 BeginPlay"), bBeganPlay))
	{
		TestWorld.ForwardErrorMessages(&Test);
		return false;
	}
	Test.TestTrue(TEXT("시작 후 초기화 성공"), Target->IsStatsInitialized());
	if constexpr (TIsDerivedFrom<ActorT, APawn>::Value)
	{
		Test.TestTrue(TEXT("부모 BeginPlay 시점 관찰"), bObservedPawnBeginPlay);
		Test.TestTrue(TEXT("부모 BeginPlay 안에서 이미 수치 준비됨"), bReadyDuringPawnBeginPlay);
	}
	Test.TestTrue(TEXT("시작 후 수치 조회"), Target->TryGetStats(Actual));
	Test.TestTrue(TEXT("모든 필드가 테이블 행과 일치"), RowT::StaticStruct()->CompareScriptStruct(&Actual, Expected, 0));
	// Blueprint에서 사용하는 동일한 반영 함수를 호출하여 노출 경로도 확인한다.
	struct FIsInitializedParameters { bool ReturnValue = false; } Parameters;
	UFunction* Query = Target->FindFunction(TEXT("IsStatsInitialized"));
	if (Test.TestNotNull(TEXT("Blueprint 초기화 상태 조회 함수"), Query))
	{
		Target->ProcessEvent(Query, &Parameters);
		Test.TestTrue(TEXT("Blueprint 호출 경로의 성공 상태"), Parameters.ReturnValue);
	}
	TestWorld.ForwardErrorMessages(&Test);
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatTargetAssetsTest, "Landship.Combat.TargetAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatTargetAssetsTest::RunTest(const FString& Parameters)
{
	CheckBlueprintTarget<ALSUnit, FLSUnitStatRow>(*this,
		TEXT("/Game/Developers/BHJ/CombatStats/BP_TestUnit.BP_TestUnit_C"),
		TEXT("/Game/Landship/Combat/Data/DT_UnitStats.DT_UnitStats"), LSCollisionChannels::Unit);
	CheckBlueprintTarget<ALSCastle, FLSCastleStatRow>(*this,
		TEXT("/Game/Developers/BHJ/CombatStats/BP_TestCastle.BP_TestCastle_C"),
		TEXT("/Game/Landship/Combat/Data/DT_CastleStats.DT_CastleStats"), LSCollisionChannels::Building);
	CheckBlueprintTarget<ALSOutpost, FLSOutpostStatRow>(*this,
		TEXT("/Game/Developers/BHJ/CombatStats/BP_TestOutpost.BP_TestOutpost_C"),
		TEXT("/Game/Landship/Combat/Data/DT_OutpostStats.DT_OutpostStats"), LSCollisionChannels::Building);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCombatTargetFailureTest, "Landship.Combat.TargetFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCombatTargetFailureTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("실패 경로 게임 월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	ALSOutpost* Target = TestWorld.GetTestWorld()->SpawnActor<ALSOutpost>();
	if (!TestNotNull(TEXT("테이블 미지정 거점 생성"), Target))
	{
		return false;
	}
	const FString ExpectedPrefix = TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed:");
	AddExpectedErrorPlain(ExpectedPrefix, EAutomationExpectedErrorFlags::Contains, 1);
	FEnsureScope ExpectedEnsure([this, &ExpectedPrefix](const FEnsureHandlerArgs& Args)
	{
		if (!FString(Args.Message).StartsWith(ExpectedPrefix))
		{
			return false;
		}
		AddError(Args.Message);
		return true;
	});
	TestTrue(TEXT("실패 대상도 진단 후 BeginPlay 완료"), TestWorld.BeginPlayInTestWorld());
	TestEqual(TEXT("실제 ensure 검출 횟수"), ExpectedEnsure.GetCount(), 1);
	TestEqual(TEXT("실패 원인 보관"), Target->GetStatsInitResult(), ELSCombatStatsInitResult::MissingTable);
	TestFalse(TEXT("초기화 실패 유지"), Target->IsStatsInitialized());
	FLSOutpostStatRow Output;
	Output.Health = 4242.0f;
	TestFalse(TEXT("실패한 수치 조회 거부"), Target->TryGetStats(Output));
	TestEqual(TEXT("실패 조회가 출력값을 바꾸지 않음"), Output.Health, 4242.0f);
	TestFalse(TEXT("실패 액터 자동 제거 안 함"), Target->IsActorBeingDestroyed());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
