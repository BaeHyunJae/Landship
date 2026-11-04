#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Match/LSAutoBattleScenario.h"
#include "Match/LSAutoBattleEnemyMarker.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAutoBattlePlacementToolTest, "Landship.AutoBattle.EnemyPlacementTool",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAutoBattlePlacementToolTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!World)
	{
		return false;
	}
	auto* Floor = World->SpawnActor<AStaticMeshActor>();
	Floor->SetActorLocation(FVector(0, 0, -25));
	Floor->GetStaticMeshComponent()->SetStaticMesh(
	    LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(52, 40, 0.5));
	Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->GetStaticMeshComponent()->SetCollisionObjectType(LSCollisionChannels::Ground);
	auto* Scenario = World->SpawnActor<ALSAutoBattleScenario>();
	if (!Scenario)
	{
		return false;
	}
	auto Markers = [World, Scenario]()
	{
		TArray<ALSAutoBattleEnemyMarker*> Result;
		for (TActorIterator<ALSAutoBattleEnemyMarker> It(World); It; ++It)
		{
			if (It->Scenario == Scenario && !It->IsActorBeingDestroyed())
			{
				Result.Add(*It);
			}
		}
		return Result;
	};
	Scenario->LoadEnemyMarkers();
	TestEqual(TEXT("저장된 편성에서 에디터 마커 생성"), Markers().Num(), 6);
	Scenario->LoadEnemyMarkers();
	TestEqual(TEXT("중복 불러오기는 편집을 덮어쓰지 않음"), Markers().Num(), 6);
	Scenario->PlacementTroopType = ELSTroopType::Cavalry;
	Scenario->AddEnemyMarker();
	TestEqual(TEXT("빈 자리로 새 적 추가"), Markers().Num(), 7);
	Scenario->ApplyEnemyMarkers();
	TestEqual(TEXT("마커 편성 적용"), Scenario->EnemyFormation.Num(), 7);
	TestTrue(TEXT("설정 검사 유효"), Scenario->GetConfigurationError().IsEmpty());
	ALSAutoBattleEnemyMarker* Marker = Markers()[0];
	const FVector Before = Marker->GetActorLocation();
	Marker->SetActorLocation(FVector(99999, 0, 0));
	Scenario->ApplyEnemyMarkers();
	TestEqual(TEXT("범위 밖 적용 실패 시 기존 편성 유지"), Scenario->EnemyFormation.Num(), 7);
	TestTrue(TEXT("실패 원인 표시"), Scenario->PlacementToolStatus.Contains(TEXT("기존 편성 유지")));
	Marker->SetActorLocation(Markers()[1]->GetActorLocation());
	Scenario->ApplyEnemyMarkers();
	TestTrue(TEXT("마커 간 겹침 거부"), Scenario->PlacementToolStatus.Contains(TEXT("병력이 겹칩니다")));
	Marker->SetActorLocation(Before);
	Marker->SetActorScale3D(FVector(2));
	Scenario->ApplyEnemyMarkers();
	TestTrue(TEXT("지원하지 않는 스케일 거부"), Scenario->PlacementToolStatus.Contains(TEXT("스케일")));
	Marker->SetActorScale3D(FVector::OneVector);
	const ELSTroopType OldType = Marker->TroopType;
	Marker->TroopType = OldType == ELSTroopType::Archer ? ELSTroopType::Spearman : ELSTroopType::Archer;
	Marker->SetActorLocation(Before + FVector(0, 0, 100));
	const auto Snapshot = Scenario->EnemyFormation;
	Scenario->ApplyEnemyMarkers();
	TestTrue(TEXT("지면에 스냅"), FMath::IsNearlyZero(Marker->GetActorLocation().Z));
	TestTrue(TEXT("적용 트랜잭션 Undo"), GEditor->UndoTransaction());
	bool bSame = Snapshot.Num() == Scenario->EnemyFormation.Num();
	for (int32 Index = 0; bSame && Index < Snapshot.Num(); ++Index)
	{
		bSame = Snapshot[Index].Type == Scenario->EnemyFormation[Index].Type &&
		        Snapshot[Index].Position.Equals(Scenario->EnemyFormation[Index].Position);
	}
	TestTrue(TEXT("Undo 시 원래 편성 복원"), bSame);
	TestTrue(TEXT("적용 트랜잭션 Redo"), GEditor->RedoTransaction());
	Scenario->HideEnemyMarkers();
	TestEqual(TEXT("마커 숨김은 실제 편성을 삭제하지 않음"), Scenario->EnemyFormation.Num(), 7);
	TestEqual(TEXT("숨긴 마커 정리"), Markers().Num(), 0);
	TestTrue(TEXT("마커 삭제 Undo"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo로 마커 복구"), Markers().Num(), 7);
	TestTrue(TEXT("마커는 에디터 전용"), Marker->IsEditorOnly());
	GEditor->SelectNone(false, true);
	for (auto* Existing : Markers())
	{
		World->EditorDestroyActor(Existing, true);
	}
	Scenario->AddEnemyMarker();
	TestEqual(TEXT("편집 중 전부 지운 뒤에도 새 적을 추가할 수 있음"), Markers().Num(), 1);
	Scenario->ApplyEnemyMarkers();
	TestEqual(TEXT("새 편성 한 기로 교체"), Scenario->EnemyFormation.Num(), 1);
	return true;
}
#endif
