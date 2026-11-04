#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Castle/LSCastle.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Tests/AutomationCommon.h"
#include "Vision/LSVisionParticipantComponent.h"

namespace
{
template <typename T>
T* SpawnSetupValidationActor(UWorld* World)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
}

bool HasValidationError(const FDataValidationContext& Context, const TCHAR* Text)
{
	for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
	{
		if (Issue.Severity == EMessageSeverity::Error && Issue.Message.ToString().Contains(Text))
		{
			return true;
		}
	}
	return false;
}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldActualCastleValidationTest,
	"Landship.Enemies.Battlefield.ActualCastleValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldActualCastleValidationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Setup = SpawnSetupValidationActor<ALSBattlefieldSetup>(World);
	Setup->CoreOutpost = SpawnSetupValidationActor<ALSOutpost>(World);
	Setup->PlayerCastle = LSCastleTestFixture::Spawn(World);
	TestTrue(TEXT("actual castle satisfies authoritative setup"), Setup->GetConfigurationError().IsEmpty());
	Setup->PlayerCastle = SpawnSetupValidationActor<ALSMobileCastle>(World);
	TestTrue(TEXT("movement-test MobileCastle explicitly rejected"), Setup->GetConfigurationError().Contains(TEXT("actual ALSCastle")));
	FDataValidationContext MobileValidation;
	TestEqual(TEXT("editor also rejects movement-test MobileCastle"), Setup->IsDataValid(MobileValidation), EDataValidationResult::Invalid);
	auto* Fake = SpawnSetupValidationActor<ALSBasicAttackTestActor>(World);
	auto* Selection = NewObject<ULSSelectableComponent>(Fake);
	Selection->Kind = ELSSelectionKind::Castle;
	Fake->AddInstanceComponent(Selection);
	Selection->RegisterComponent();
	Setup->PlayerCastle = Fake;
	TestTrue(TEXT("generic actor with Ally faction, Castle selection and health is rejected"), Setup->GetConfigurationError().Contains(TEXT("actual ALSCastle")));
	Setup->PlayerCastle = LSCastleTestFixture::Spawn(World);
	TestTrue(TEXT("restoring actual castle restores valid configuration"), Setup->GetConfigurationError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldCoreMarkerValidationTest,
	"Landship.Enemies.Battlefield.CoreMarkerValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldCoreMarkerValidationTest::RunTest(const FString& Parameters)
{
	struct FLSCoreMarkerValidationCase
	{
		const TCHAR* Name;
		bool bSettings;
		bool bCoreFlag;
		bool bOtherFlag;
		EDataValidationResult Expected;
	};
	const FLSCoreMarkerValidationCase Cases[] = {
		{TEXT("올바른 핵심 표식"), true, true, false, EDataValidationResult::Valid},
		{TEXT("핵심 표식 누락"), true, false, false, EDataValidationResult::Invalid},
		{TEXT("다른 거점에만 표식"), true, false, true, EDataValidationResult::Invalid},
		{TEXT("핵심과 다른 거점에 중복 표식"), true, true, true, EDataValidationResult::Invalid},
		{TEXT("시야 설정 없는 기존 월드"), false, false, true, EDataValidationResult::Valid},
	};
	for (const FLSCoreMarkerValidationCase& Case : Cases)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSBattlefieldSetup* Setup = SpawnSetupValidationActor<ALSBattlefieldSetup>(World);
		Setup->PlayerCastle = SpawnSetupValidationActor<ALSCastle>(World);
		Setup->CoreOutpost = SpawnSetupValidationActor<ALSOutpost>(World);
		ALSOutpost* OtherOutpost = SpawnSetupValidationActor<ALSOutpost>(World);
		Setup->CoreOutpost->Vision->bPublicCorePosition = Case.bCoreFlag;
		OtherOutpost->Vision->bPublicCorePosition = Case.bOtherFlag;
		if (Case.bSettings)
		{
			SpawnSetupValidationActor<ALSBattlefieldSettings>(World);
		}
		FDataValidationContext Validation;
		TestEqual(Case.Name, Setup->IsDataValid(Validation), Case.Expected);
		if (Case.Expected == EDataValidationResult::Valid)
		{
			TestEqual(FString::Printf(TEXT("%s: 오류 없음"), Case.Name), Validation.GetNumErrors(), 0u);
		}
		else
		{
			if (!Case.bCoreFlag)
			{
				TestTrue(FString::Printf(TEXT("%s: 지정 핵심의 누락 진단"), Case.Name),
					HasValidationError(Validation, TEXT("CoreOutpost must have")));
			}
			if (Case.bOtherFlag)
			{
				TestTrue(FString::Printf(TEXT("%s: 다른 액터의 표식 진단"), Case.Name),
					HasValidationError(Validation, TEXT("Only CoreOutpost")));
			}
		}
		TestEqual(FString::Printf(TEXT("%s: 핵심 표식 자동 변경 없음"), Case.Name),
			!!Setup->CoreOutpost->Vision->bPublicCorePosition, Case.bCoreFlag);
		TestEqual(FString::Printf(TEXT("%s: 다른 표식 자동 변경 없음"), Case.Name),
			!!OtherOutpost->Vision->bPublicCorePosition, Case.bOtherFlag);
		const bool bOriginalLevelVisible = World->PersistentLevel->bIsVisible;
		World->PersistentLevel->bIsVisible = false;
		FDataValidationContext InactiveValidation;
		TestEqual(FString::Printf(TEXT("%s: 비활성 레벨에서도 같은 데이터 판정"), Case.Name),
			Setup->IsDataValid(InactiveValidation), Case.Expected);
		World->PersistentLevel->bIsVisible = bOriginalLevelVisible;
	}
	return true;
}

#endif
