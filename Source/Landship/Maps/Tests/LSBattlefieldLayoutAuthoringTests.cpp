#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Maps/LSBattlefieldLayoutAuthoring.h"
#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyTroop.h"
#include "Obstacles/LSTurret.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include <limits>

namespace
{
ALSBattlefieldLayoutAuthoring* CreateLayout(UWorld* World, AActor*& First, AActor*& Second)
{
	ALSBattlefieldLayoutAuthoring* Layout = World->SpawnActor<ALSBattlefieldLayoutAuthoring>();
	First = World->SpawnActor<AStaticMeshActor>();
	Second = World->SpawnActor<ATargetPoint>();
	if (!Layout || !First || !Second)
	{
		return nullptr;
	}
	Layout->Registry.SetNum(2);
	Layout->Registry[0].ActorId = TEXT("Enemy");
	Layout->Registry[0].Actor = First;
	Layout->Registry[1].ActorId = TEXT("Obstacle");
	Layout->Registry[1].Actor = Second;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FLSBattlefieldLayoutPreset& Preset = Layout->Presets.AddDefaulted_GetRef();
		Preset.PresetId = FName(*FString::Printf(TEXT("V%d"), Index));
		Preset.Placements.Add(TEXT("Enemy"), FTransform(FRotator(0, Index * 35, 0), FVector(Index * 150, 75, 20), FVector(1.0 + Index * 0.25)));
		Preset.Placements.Add(TEXT("Obstacle"), FTransform(FRotator(0, -Index * 50, 0), FVector(-80, Index * 200, 30), FVector(1.0, 1.5, 2.0 + Index)));
	}
	Layout->SelectedPreset = TEXT("V0");
	return Layout;
}

int32 CountLayoutWorldActors(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++Count;
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutRoundTripTest,
	"Landship.Maps.Layout.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutRoundTripTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("에디터 월드 생성"), TestWorld.CreateTestWorld(EWorldType::Editor))) return false;
	AActor* First = nullptr;
	AActor* Second = nullptr;
	ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(TestWorld.GetTestWorld(), First, Second);
	if (!TestNotNull(TEXT("배치 설정과 대상 생성"), Layout)) return false;
	AActor* Unregistered = TestWorld.GetTestWorld()->SpawnActor<ATargetPoint>();
	if (!TestNotNull(TEXT("배치 대상 밖의 액터 생성"), Unregistered)) return false;
	Unregistered->SetActorLocation(FVector(900, 800, 700));
	const FTransform UnregisteredTransform = Unregistered->GetActorTransform();
	const int32 OriginalActorCount = CountLayoutWorldActors(TestWorld.GetTestWorld());
	UClass* FirstClass = First->GetClass();
	UClass* SecondClass = Second->GetClass();
	USceneComponent* FirstRoot = First->GetRootComponent();
	USceneComponent* SecondRoot = Second->GetRootComponent();
	First->Tags.Add(TEXT("PreserveEnemyData"));
	Second->Tags.Add(TEXT("PreserveObstacleData"));
	First->SetCanBeDamaged(false);
	Second->SetActorEnableCollision(false);
	TestTrue(TEXT("쿠킹에서 제외되는 액터"), Layout->IsEditorOnly());
	TestFalse(TEXT("Tick을 사용하지 않음"), Layout->PrimaryActorTick.bCanEverTick);
	const int32 Sequence[] = {0, 1, 0, 2};
	for (int32 Index : Sequence)
	{
		const FLSBattlefieldLayoutPreset& Preset = Layout->Presets[Index];
		Layout->SelectedPreset = Preset.PresetId;
		TestTrue(TEXT("적용 전 전체 설정 유효"), Layout->GetConfigurationError().IsEmpty());
		Layout->ApplySelectedPreset();
		TestEqual(TEXT("실제 배치와 선택한 배치안 일치"), Layout->GetMatchingPreset(), Preset.PresetId);
		TestTrue(TEXT("첫 대상의 월드 변환 적용"), First->GetActorTransform().Equals(Preset.Placements.FindChecked(TEXT("Enemy")), 0.0001));
		TestTrue(TEXT("둘째 대상의 월드 변환 적용"), Second->GetActorTransform().Equals(Preset.Placements.FindChecked(TEXT("Obstacle")), 0.0001));
		TestEqual(TEXT("첫 대상 참조 유지"), Layout->Registry[0].Actor.Get(), First);
		TestEqual(TEXT("둘째 대상 참조 유지"), Layout->Registry[1].Actor.Get(), Second);
		TestEqual(TEXT("첫 대상 클래스 유지"), First->GetClass(), FirstClass);
		TestEqual(TEXT("둘째 대상 클래스 유지"), Second->GetClass(), SecondClass);
		TestEqual(TEXT("첫 대상 컴포넌트 유지"), First->GetRootComponent(), FirstRoot);
		TestEqual(TEXT("둘째 대상 컴포넌트 유지"), Second->GetRootComponent(), SecondRoot);
		TestTrue(TEXT("첫 대상의 다른 속성 유지"), First->Tags.Contains(TEXT("PreserveEnemyData")) && !First->CanBeDamaged());
		TestTrue(TEXT("둘째 대상의 다른 속성 유지"), Second->Tags.Contains(TEXT("PreserveObstacleData")) && !Second->GetActorEnableCollision());
		TestTrue(TEXT("등록하지 않은 액터의 배치 유지"), Unregistered->GetActorTransform().Equals(UnregisteredTransform));
		TestEqual(TEXT("액터 생성·제거 없음"), CountLayoutWorldActors(TestWorld.GetTestWorld()), OriginalActorCount);
	}
	FDataValidationContext Validation;
	TestEqual(TEXT("에디터 유효성 검사 성공"), Layout->IsDataValid(Validation), EDataValidationResult::Valid);
	TestEqual(TEXT("유효성 오류 없음"), Validation.GetNumErrors(), 0u);
	Layout->ValidateLayout();
	TestTrue(TEXT("검사는 위치를 바꾸지 않음"), First->GetActorTransform().Equals(Layout->Presets[2].Placements.FindChecked(TEXT("Enemy"))));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutRejectInvalidTest,
	"Landship.Maps.Layout.RejectInvalidWithoutMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutRejectInvalidTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	FTestWorldWrapper OtherWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Editor) || !OtherWorld.CreateTestWorld(EWorldType::Editor)) return false;
	AActor* First = nullptr;
	AActor* Second = nullptr;
	ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(TestWorld.GetTestWorld(), First, Second);
	if (!TestNotNull(TEXT("배치 설정 생성"), Layout)) return false;
	AActor* ForeignActor = OtherWorld.GetTestWorld()->SpawnActor<ATargetPoint>();
	AActor* RootlessActor = TestWorld.GetTestWorld()->SpawnActor<AActor>();
	if (!ForeignActor || !RootlessActor) return false;
	First->SetActorLocation(FVector(300, 400, 500));
	Second->SetActorLocation(FVector(-300, -400, -500));
	const FTransform FirstTransform = First->GetActorTransform();
	const FTransform SecondTransform = Second->GetActorTransform();
	const TArray<FLSBattlefieldLayoutActorEntry> ValidRegistry = Layout->Registry;
	const TArray<FLSBattlefieldLayoutPreset> ValidPresets = Layout->Presets;
	struct FLSInvalidLayoutCase
	{
		const TCHAR* Name;
		TFunction<void()> Corrupt;
	};
	const FLSInvalidLayoutCase Cases[] = {
		{TEXT("빈 등록 목록"), [&] { Layout->Registry.Empty(); }},
		{TEXT("빈 ActorId"), [&] { Layout->Registry[1].ActorId = NAME_None; }},
		{TEXT("중복 ActorId"), [&] { Layout->Registry[1].ActorId = Layout->Registry[0].ActorId; }},
		{TEXT("null 대상"), [&] { Layout->Registry[1].Actor = nullptr; }},
		{TEXT("중복 대상"), [&] { Layout->Registry[1].Actor = First; }},
		{TEXT("자기 자신 등록"), [&] { Layout->Registry[1].Actor = Layout; }},
		{TEXT("다른 월드 대상"), [&] { Layout->Registry[1].Actor = ForeignActor; }},
		{TEXT("루트 없는 대상"), [&] { Layout->Registry[1].Actor = RootlessActor; }},
		{TEXT("빈 PresetId"), [&] { Layout->Presets[1].PresetId = NAME_None; }},
		{TEXT("중복 PresetId"), [&] { Layout->Presets[1].PresetId = Layout->Presets[0].PresetId; }},
		{TEXT("배치 누락"), [&] { Layout->Presets[1].Placements.Remove(TEXT("Obstacle")); }},
		{TEXT("잘못된 배치 ID"), [&] {
			Layout->Presets[1].Placements.Remove(TEXT("Obstacle"));
			Layout->Presets[1].Placements.Add(TEXT("Unknown"), FTransform::Identity);
		}},
		{TEXT("불필요한 배치 ID"), [&] { Layout->Presets[1].Placements.Add(TEXT("Unknown"), FTransform::Identity); }},
		{TEXT("0 크기"), [&] { Layout->Presets[1].Placements[TEXT("Obstacle")].SetScale3D(FVector(1, 0, 1)); }},
		{TEXT("음수 크기"), [&] { Layout->Presets[1].Placements[TEXT("Obstacle")].SetScale3D(FVector(1, 1, -1)); }},
		{TEXT("정규화하지 않은 회전"), [&] { Layout->Presets[1].Placements[TEXT("Obstacle")].SetRotation(FQuat(0, 0, 0, 0)); }},
		{TEXT("유한하지 않은 위치"), [&] { Layout->Presets[1].Placements[TEXT("Obstacle")].SetTranslation(FVector(std::numeric_limits<double>::infinity(), 0, 0)); }},
		{TEXT("없는 선택"), [&] { Layout->SelectedPreset = TEXT("Unknown"); }},
		{TEXT("빈 선택"), [&] { Layout->SelectedPreset = NAME_None; }},
	};
	for (const FLSInvalidLayoutCase& Case : Cases)
	{
		Layout->Registry = ValidRegistry;
		Layout->Presets = ValidPresets;
		Layout->SelectedPreset = TEXT("V0");
		Case.Corrupt();
		TestFalse(Case.Name, Layout->GetConfigurationError().IsEmpty());
		FDataValidationContext Validation;
		TestEqual(Case.Name, Layout->IsDataValid(Validation), EDataValidationResult::Invalid);
		TestTrue(TEXT("오류 이유를 에디터 검사에 보고"), Validation.GetNumErrors() > 0);
		Layout->ApplySelectedPreset();
		TestTrue(Case.Name, First->GetActorTransform().Equals(FirstTransform));
		TestTrue(Case.Name, Second->GetActorTransform().Equals(SecondTransform));
	}
	Layout->Registry = ValidRegistry;
	Layout->Presets = ValidPresets;
	Layout->SelectedPreset = TEXT("V0");
	Second->AttachToActor(First, FAttachmentTransformRules::KeepWorldTransform);
	TestFalse(TEXT("액터 부착 관계 거부"), Layout->GetConfigurationError().IsEmpty());
	Layout->ApplySelectedPreset();
	TestTrue(TEXT("부착 관계 거부 시 첫 대상 유지"), First->GetActorTransform().Equals(FirstTransform));
	TestTrue(TEXT("부착 관계 거부 시 둘째 대상 유지"), Second->GetActorTransform().Equals(SecondTransform));
	Second->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	TestWorld.ForwardErrorMessages(this);
	OtherWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutUndoTest,
	"Landship.Maps.Layout.UndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutUndoTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("트랜잭션을 실행할 에디터"), GEditor)) return false;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Editor)) return false;
	AActor* First = nullptr;
	AActor* Second = nullptr;
	ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(TestWorld.GetTestWorld(), First, Second);
	if (!Layout) return false;
	First->SetFlags(RF_Transactional);
	Second->SetFlags(RF_Transactional);
	First->GetRootComponent()->SetFlags(RF_Transactional);
	Second->GetRootComponent()->SetFlags(RF_Transactional);
	Layout->ApplySelectedPreset();
	TestEqual(TEXT("실행 취소 전 원래 배치는 V0"), Layout->GetMatchingPreset(), FName(TEXT("V0")));
	const FTransform FirstBefore = First->GetActorTransform();
	const FTransform SecondBefore = Second->GetActorTransform();
	Layout->SelectedPreset = TEXT("V2");
	Layout->ApplySelectedPreset();
	const FTransform FirstAfter = First->GetActorTransform();
	const FTransform SecondAfter = Second->GetActorTransform();
	TestFalse(TEXT("첫 대상 위치가 실제로 바뀜"), FirstAfter.Equals(FirstBefore));
	TestFalse(TEXT("둘째 대상 위치가 실제로 바뀜"), SecondAfter.Equals(SecondBefore));
	if (!TestTrue(TEXT("배치 전체를 한 번에 실행 취소"), GEditor->UndoTransaction())) return false;
	TestTrue(TEXT("첫 대상 변환 복구"), First->GetActorTransform().Equals(FirstBefore));
	TestTrue(TEXT("둘째 대상 변환 복구"), Second->GetActorTransform().Equals(SecondBefore));
	TestEqual(TEXT("선택은 V2인 채 실제 배치는 V0으로 복구"), Layout->GetMatchingPreset(), FName(TEXT("V0")));
	TestEqual(TEXT("실행 취소가 다음 선택을 바꾸지 않음"), Layout->SelectedPreset, FName(TEXT("V2")));
	if (!TestTrue(TEXT("배치 전체를 한 번에 다시 실행"), GEditor->RedoTransaction())) return false;
	TestTrue(TEXT("첫 대상 변환 재적용"), First->GetActorTransform().Equals(FirstAfter));
	TestTrue(TEXT("둘째 대상 변환 재적용"), Second->GetActorTransform().Equals(SecondAfter));
	TestEqual(TEXT("다시 실행 후 실제 배치는 V2"), Layout->GetMatchingPreset(), FName(TEXT("V2")));
	TestEqual(TEXT("실행 취소 뒤 첫 대상 참조 유지"), Layout->Registry[0].Actor.Get(), First);
	TestEqual(TEXT("실행 취소 뒤 둘째 대상 참조 유지"), Layout->Registry[1].Actor.Get(), Second);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutMatchingPresetTest,
	"Landship.Maps.Layout.MatchingPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutMatchingPresetTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Editor)) return false;
	AActor* First = nullptr;
	AActor* Second = nullptr;
	ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(TestWorld.GetTestWorld(), First, Second);
	if (!Layout) return false;
	const TArray<FString> Options = Layout->GetPresetOptions();
	TestEqual(TEXT("선택 목록에는 배치안 세 개 제공"), Options.Num(), 3);
	TestTrue(TEXT("선택 목록은 실제 배치안 이름 사용"), Options.Contains(TEXT("V0")) && Options.Contains(TEXT("V1")) && Options.Contains(TEXT("V2")));
	for (const FName Preset : {FName(TEXT("V0")), FName(TEXT("V1")), FName(TEXT("V0"))})
	{
		Layout->SelectedPreset = Preset;
		Layout->ApplySelectedPreset();
		TestEqual(TEXT("왕복 적용 후 실제 배치 판정"), Layout->GetMatchingPreset(), Preset);
	}
	Layout->SelectedPreset = TEXT("V1");
	TestEqual(TEXT("선택만 바꿔도 실제 배치는 V0"), Layout->GetMatchingPreset(), FName(TEXT("V0")));
	First->AddActorWorldOffset(FVector(10, 0, 0));
	TestTrue(TEXT("하나만 수동 이동한 혼합 배치는 Custom"), Layout->GetMatchingPreset().IsNone());
	TestTrue(TEXT("수동 배치도 설정 데이터는 유효"), Layout->GetConfigurationError().IsEmpty());
	Layout->ApplySelectedPreset();
	TestEqual(TEXT("Custom에서 다시 배치안 적용"), Layout->GetMatchingPreset(), FName(TEXT("V1")));
	Layout->SelectedPreset = NAME_None;
	TestEqual(TEXT("선택이 비어도 실제 배치 판정 유지"), Layout->GetMatchingPreset(), FName(TEXT("V1")));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutPlayGuardTest,
	"Landship.Maps.Layout.RejectPlayWorlds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutPlayGuardTest::RunTest(const FString& Parameters)
{
	for (const EWorldType::Type WorldType : {EWorldType::Game, EWorldType::EditorPreview, EWorldType::Inactive})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(WorldType)) return false;
		AActor* First = nullptr;
		AActor* Second = nullptr;
		ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(TestWorld.GetTestWorld(), First, Second);
		if (!Layout) return false;
		const FTransform Before = First->GetActorTransform();
		FString Error;
		TestFalse(TEXT("에디터 이외의 월드에서 적용 거부"), Layout->CanApplyPreset(Layout->SelectedPreset, Error));
		TestFalse(TEXT("적용 거부 이유 제공"), Error.IsEmpty());
		TestTrue(TEXT("에디터 이외의 월드에서도 데이터는 유효"), Layout->GetConfigurationError().IsEmpty());
		FDataValidationContext Validation;
		TestEqual(TEXT("Inactive와 비편집 월드의 데이터 검사 유효"), Layout->IsDataValid(Validation), EDataValidationResult::Valid);
		Layout->ApplySelectedPreset();
		TestTrue(TEXT("에디터 이외의 월드에서 변환 유지"), First->GetActorTransform().Equals(Before));
		TestWorld.ForwardErrorMessages(this);
	}
	if (!TestNotNull(TEXT("PIE 상태를 검사할 에디터"), GEditor)) return false;
	FTestWorldWrapper EditorWorld;
	FTestWorldWrapper PlayWorld;
	if (!EditorWorld.CreateTestWorld(EWorldType::Editor) || !PlayWorld.CreateTestWorld(EWorldType::Editor)) return false;
	AActor* First = nullptr;
	AActor* Second = nullptr;
	ALSBattlefieldLayoutAuthoring* Layout = CreateLayout(EditorWorld.GetTestWorld(), First, Second);
	if (!Layout) return false;
	const FTransform Before = First->GetActorTransform();
	{
		// FTestWorldWrapper는 PIE 생성을 지원하지 않으므로 검사 대상 월드의 유형만 바꾼다.
		TGuardValue<TEnumAsByte<EWorldType::Type>> WorldTypeGuard(
			EditorWorld.GetTestWorld()->WorldType, TEnumAsByte<EWorldType::Type>(EWorldType::PIE));
		FString Error;
		TestFalse(TEXT("PIE 월드에서 직접 적용 거부"), Layout->CanApplyPreset(Layout->SelectedPreset, Error));
		TestFalse(TEXT("PIE 월드 적용 거부 이유 제공"), Error.IsEmpty());
		TestTrue(TEXT("PIE 월드에서 설정 데이터는 유효"), Layout->GetConfigurationError().IsEmpty());
		FDataValidationContext Validation;
		TestEqual(TEXT("PIE 월드의 데이터 유효성 검사 성공"), Layout->IsDataValid(Validation), EDataValidationResult::Valid);
		Layout->ApplySelectedPreset();
		TestTrue(TEXT("PIE 월드의 액터 변환 유지"), First->GetActorTransform().Equals(Before));
	}
	{
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, PlayWorld.GetTestWorld());
		FString Error;
		TestFalse(TEXT("PIE 중 원본 에디터 월드에서도 거부"), Layout->CanApplyPreset(Layout->SelectedPreset, Error));
		TestFalse(TEXT("PIE 중 원본 월드 적용 거부 이유 제공"), Error.IsEmpty());
		TestTrue(TEXT("PIE 중 원본 월드의 데이터는 유효"), Layout->GetConfigurationError().IsEmpty());
		FDataValidationContext Validation;
		TestEqual(TEXT("PIE 중 원본 데이터 유효성 검사 성공"), Layout->IsDataValid(Validation), EDataValidationResult::Valid);
		Layout->ApplySelectedPreset();
		TestTrue(TEXT("PIE 중 원본 액터 변환 유지"), First->GetActorTransform().Equals(Before));
	}
	FString Error;
	TestTrue(TEXT("PIE 상태 복원 뒤 적용 가능"), Layout->CanApplyPreset(Layout->SelectedPreset, Error));
	TestTrue(TEXT("적용 가능하면 이전 오류 제거"), Error.IsEmpty());
	EditorWorld.ForwardErrorMessages(this);
	PlayWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldLayoutFrontMouthTest,
	"Landship.Maps.Layout.FrontMouthPresetClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldLayoutFrontMouthTest::RunTest(const FString& Parameters)
{
	// 원본 월드를 열거나 배치안을 적용하지 않고 저장된 액터 참조와 후보 변환만 읽는다.
	UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Landship/Maps/L_BattlefieldPrototype.L_BattlefieldPrototype"));
	if (!TestNotNull(TEXT("현재 전장 맵 데이터 로드"), World)) return false;
	ALSBattlefieldLayoutAuthoring* Layout = nullptr;
	for (TActorIterator<ALSBattlefieldLayoutAuthoring> It(World); It; ++It) { Layout = *It; break; }
	if (!TestNotNull(TEXT("현재 전장 배치 설정"), Layout) ||
		!TestNotNull(TEXT("현재 전장 지역 설정"), Layout->EncounterCoordinator.Get())) return false;
	if (!TestTrue(TEXT("현재 프리셋 전체는 네이티브 설정 검사 통과"), Layout->GetConfigurationError().IsEmpty())) return false;
	const bool bWasDirty = World->GetOutermost()->IsDirty();
	const int32 ActorCount = CountLayoutWorldActors(World);
	TMap<AActor*, FTransform> OriginalTransforms;
	for (const FLSBattlefieldLayoutActorEntry& Entry : Layout->Registry)
		OriginalTransforms.Add(Entry.Actor.Get(), Entry.Actor->GetActorTransform());
	const auto& Profiles = GetDefault<ULSBattlefieldNavigationSystem>()->GetSupportedAgents();
	const FNavDataConfig* Troops = Profiles.FindByPredicate([](const FNavDataConfig& Profile) { return Profile.Name == TEXT("Troops"); });
	if (!TestNotNull(TEXT("전장 병력 내비게이션 프로필"), Troops)) return false;
	for (const FLSBattlefieldLayoutPreset& Preset : Layout->Presets)
	{
		TMap<FName, FTransform> Transforms;
		for (const FLSBattlefieldLayoutActorEntry& Entry : Layout->Registry)
			Transforms.Add(Entry.Actor->GetFName(), Preset.Placements.FindChecked(Entry.ActorId));
		FString Error;
		TestTrue(Preset.PresetId.ToString() + TEXT(" 현재 프리셋 통과"), Layout->EncounterCoordinator->ValidatePresetTransforms(Transforms, Error));
		TSet<FString> GuardCases;
		TSet<FName> GuardRegions, TurretRegions;
		for (const FLSEncounterRegion& Region : Layout->EncounterCoordinator->Regions)
		{
			if (Region.RegionId != TEXT("L1") && Region.RegionId != TEXT("C1") && Region.RegionId != TEXT("R1")) continue;
			for (AActor* Target : Region.AlarmTargets)
			{
				if (const auto* Guard = Cast<ALSEnemyTroop>(Target); Guard && Guard->Behavior->Role == ELSEnemyRole::Guard)
				{
					const FString Case = Region.RegionId.ToString() + FString::SanitizeFloat(Guard->GetCapsuleComponent()->GetUnscaledCapsuleRadius());
					if (GuardCases.Contains(Case)) continue;
					GuardCases.Add(Case);
					GuardRegions.Add(Region.RegionId);
					const FTransform Original = Transforms.FindChecked(Target->GetFName());
					const FVector Scale = Original.GetScale3D();
					const double Radius = Guard->GetCapsuleComponent()->GetUnscaledCapsuleRadius() * FMath::Min(Scale.X, Scale.Y);
					for (double X : {3700.0, 4200.0 + Troops->AgentRadius + Radius - 1.0})
					{
						FTransform Candidate = Original;
						FVector Position = Candidate.GetLocation(); Position.X = X; Candidate.SetLocation(Position);
						Transforms[Target->GetFName()] = Candidate;
						TestFalse(Case + TEXT(" 서쪽 입구 또는 몸체 여유 침범 거부"), Layout->EncounterCoordinator->ValidatePresetTransforms(Transforms, Error));
						TestTrue(Case + TEXT(" 전방 Guard 제약으로 거부"), Error.Contains(TEXT("front Guard")));
					}
					Transforms[Target->GetFName()] = Original;
				}
				if (Target->IsA<ALSTurret>() && !TurretRegions.Contains(Region.RegionId))
				{
					TurretRegions.Add(Region.RegionId);
					const FTransform Original = Transforms.FindChecked(Target->GetFName());
					FTransform Candidate = Original;
					FVector Position = Candidate.GetLocation(); Position.X = 4210; Candidate.SetLocation(Position);
					Transforms[Target->GetFName()] = Candidate;
					TestFalse(Region.RegionId.ToString() + TEXT(" 중심은 동쪽이어도 터렛 몸체 침범 거부"), Layout->EncounterCoordinator->ValidatePresetTransforms(Transforms, Error));
					TestTrue(TEXT("터렛 권위 몸체 제약으로 거부"), Error.Contains(TEXT("front turret body")));
					Candidate.SetLocation(FVector(4400, Original.GetLocation().Y, Original.GetLocation().Z));
					Candidate.SetRotation(FQuat(FRotator(0, 45, 0)));
					Candidate.SetScale3D(FVector(4));
					Transforms[Target->GetFName()] = Candidate;
					TestFalse(TEXT("터렛 후보 회전·크기를 포함한 외곽 침범 거부"), Layout->EncounterCoordinator->ValidatePresetTransforms(Transforms, Error));
					TestTrue(TEXT("회전·크기 후보도 터렛 몸체 제약으로 거부"), Error.Contains(TEXT("front turret body")));
					Transforms[Target->GetFName()] = Original;
				}
			}
		}
		TestEqual(TEXT("세 전방 지역 Guard 후보 검사"), GuardRegions.Num(), 3);
		TestEqual(TEXT("세 전방 지역 터렛 후보 검사"), TurretRegions.Num(), 3);
		TestTrue(TEXT("후보 검사 후 원본 프리셋은 계속 유효"), Layout->EncounterCoordinator->ValidatePresetTransforms(Transforms, Error));
	}
	for (const auto& Original : OriginalTransforms)
		TestTrue(TEXT("검사 중 실제 액터 변환 유지"), Original.Key->GetActorTransform().Equals(Original.Value));
	TestEqual(TEXT("검사 중 액터 생성·제거 없음"), CountLayoutWorldActors(World), ActorCount);
	TestEqual(TEXT("검사 중 맵 저장 상태 유지"), World->GetOutermost()->IsDirty(), bWasDirty);
	return true;
}

#endif
