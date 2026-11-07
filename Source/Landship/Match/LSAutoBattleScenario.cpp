#include "Match/LSAutoBattleScenario.h"
#if WITH_EDITOR
#include "Match/LSAutoBattleEnemyMarker.h"
#include "Combat/LSCollisionChannels.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#endif

ALSAutoBattleScenario::ALSAutoBattleScenario()
{
	PrimaryActorTick.bCanEverTick = false;
	const float Angles[] = {-30.0f, -15.0f, 0.0f, 15.0f, 30.0f};
	const float TroopMultipliers[] = {1.15f, 1.10f, 1.0f, 0.75f, 0.55f};
	const float CastleMultipliers[] = {1.08f, 1.05f, 1.0f, 0.82f, 0.65f};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Angles); ++Index)
	{
		TroopSlopeSpeed.GetRichCurve()->AddKey(Angles[Index], TroopMultipliers[Index]);
		CastleSlopeSpeed.GetRichCurve()->AddKey(Angles[Index], CastleMultipliers[Index]);
	}
	EnemyDetectionRanges.Add(ELSTroopType::Spearman, 650.0f);
	EnemyDetectionRanges.Add(ELSTroopType::Archer, 1400.0f);
	EnemyDetectionRanges.Add(ELSTroopType::Cavalry, 1000.0f);
	EnemyDetectionRanges.Add(ELSTroopType::Shieldbearer, 650.0f);
	EnemyDetectionRanges.Add(ELSTroopType::Mortar, 1700.0f);
	EnemyDetectionRanges.Add(ELSTroopType::Bomber, 900.0f);
	for (int32 Index = 0; Index < ALSTroopUnit::GetTroopTypeCount(); ++Index)
	{
		const ELSTroopType Type = static_cast<ELSTroopType>(Index);
		UnitCosts.Add(Type, GetDefaultUnitCost(Type));
	}
}

int32 ALSAutoBattleScenario::GetDefaultUnitCost(ELSTroopType Type)
{
	switch (Type)
	{
	case ELSTroopType::Spearman: return 40;
	case ELSTroopType::Archer: return 60;
	case ELSTroopType::Cavalry: return 120;
	case ELSTroopType::Medic: return 80;
	case ELSTroopType::Shieldbearer: return 55;
	case ELSTroopType::Mortar: return 120;
	case ELSTroopType::Bomber: return 95;
	default: return 0;
	}
}

void ALSAutoBattleScenario::PostLoad()
{
	Super::PostLoad();
	// 이전 기본 비용을 저장한 맵도 정예 기병 비용을 사용한다. 별도로 조정한 비용은 보존한다.
	if (int32* Cost = UnitCosts.Find(ELSTroopType::Cavalry); Cost && *Cost == 90)
		*Cost = GetDefaultUnitCost(ELSTroopType::Cavalry);
	// 기존 개인 맵에 저장된 비용과 감지 거리를 보존하면서 신규 병종 항목을 채운다.
	const auto* Defaults = GetDefault<ALSAutoBattleScenario>();
	for (const auto& Entry : Defaults->UnitCosts)
	{
		if (!UnitCosts.Contains(Entry.Key))
			UnitCosts.Add(Entry.Key, Entry.Value);
	}
	for (const auto& Entry : Defaults->EnemyDetectionRanges)
	{
		if (!EnemyDetectionRanges.Contains(Entry.Key))
			EnemyDetectionRanges.Add(Entry.Key, Entry.Value);
	}
}

FString ALSAutoBattleScenario::GetConfigurationError() const
{
	if (!FMath::IsFinite(TargetRetentionPadding) || TargetRetentionPadding < 0)
	{
		return TEXT("표적 유지 여유 거리가 잘못되었습니다.");
	}
	for (const auto& Entry : GetDefault<ALSAutoBattleScenario>()->EnemyDetectionRanges)
	{
		const float* Range = EnemyDetectionRanges.Find(Entry.Key);
		if (!Range || !FMath::IsFinite(*Range) || *Range <= 0)
		{
			return TEXT("모든 공격 병종에 유효한 적 감지 거리가 필요합니다.");
		}
	}
	if (BoundsMin.ContainsNaN() || BoundsMax.ContainsNaN() || DeploymentMin.ContainsNaN() ||
	    DeploymentMax.ContainsNaN() || BoundsMin.X >= BoundsMax.X || BoundsMin.Y >= BoundsMax.Y ||
	    DeploymentMin.X >= DeploymentMax.X || DeploymentMin.Y >= DeploymentMax.Y || EnemyFormation.Num() > 100 ||
	    CameraLocation.ContainsNaN() || CameraRotation.ContainsNaN())
	{
		return TEXT("시나리오 경계·적 편성·카메라 설정이 잘못되었습니다.");
	}
	const FBox2D Bounds(BoundsMin, BoundsMax);
	if (!Bounds.IsInsideOrOn(DeploymentMin) || !Bounds.IsInsideOrOn(DeploymentMax) || AllyCastleGround.ContainsNaN() ||
	    EnemyCastleGround.ContainsNaN() ||
	    !FBox2D(DeploymentMin + FVector2D(142), DeploymentMax - FVector2D(142))
	         .IsInsideOrOn(FVector2D(AllyCastleGround)) ||
	    !Bounds.IsInsideOrOn(FVector2D(EnemyCastleGround)))
	{
		return TEXT("배치 구역 또는 성 위치가 전장 경계를 벗어납니다.");
	}
	for (int32 Index = 0; Index < ALSTroopUnit::GetTroopTypeCount(); ++Index)
	{
		const int32* Cost = UnitCosts.Find(static_cast<ELSTroopType>(Index));
		if (!Cost || *Cost < 1)
		{
			return TEXT("모든 병종의 양수 비용이 필요합니다.");
		}
	}
	if (static_cast<uint8>(EnemyWeapon) > 2)
	{
		return TEXT("적 성 무장 설정이 잘못되었습니다.");
	}
	for (const FLSAutoBattlePlacement& Unit : EnemyFormation)
	{
		if (ALSTroopUnit::GetStatsRowName(Unit.Type).IsNone() || Unit.Position.ContainsNaN() ||
		    !Bounds.IsInsideOrOn(FVector2D(Unit.Position)))
		{
			return TEXT("적 병종 또는 위치가 잘못되었습니다.");
		}
	}
	return FString();
}

#if WITH_EDITOR
namespace
{
bool CanEditEnemyPlacement(const ALSAutoBattleScenario* Scenario)
{
	return GEditor && !GEditor->PlayWorld && Scenario->GetWorld() &&
	       Scenario->GetWorld()->WorldType == EWorldType::Editor;
}
TArray<ALSAutoBattleEnemyMarker*> FindEnemyMarkers(ALSAutoBattleScenario* Scenario)
{
	TArray<ALSAutoBattleEnemyMarker*> Markers;
	for (TActorIterator<ALSAutoBattleEnemyMarker> It(Scenario->GetWorld()); It; ++It)
	{
		if (It->Scenario == Scenario && !It->IsActorBeingDestroyed())
		{
			Markers.Add(*It);
		}
	}
	Markers.Sort([](const ALSAutoBattleEnemyMarker& A, const ALSAutoBattleEnemyMarker& B)
	             { return A.GetFName().LexicalLess(B.GetFName()); });
	return Markers;
}
float EnemyRadius(ELSTroopType Type)
{
	return Type == ELSTroopType::Cavalry ? 65.0f : 42.0f;
}
bool ResolveEnemyGround(ALSAutoBattleScenario* Scenario, ELSTroopType Type, const FVector& Input, FVector& Ground)
{
	if (ALSTroopUnit::GetStatsRowName(Type).IsNone() || Input.ContainsNaN())
	{
		return false;
	}
	FCollisionObjectQueryParams GroundObjects(LSCollisionChannels::Ground);
	FHitResult Hit;
	if (!Scenario->GetWorld()->LineTraceSingleByObjectType(Hit, Input + FVector(0, 0, 10000),
	                                                       Input - FVector(0, 0, 10000), GroundObjects))
	{
		return false;
	}
	Ground = Hit.ImpactPoint;
	const float Radius = EnemyRadius(Type);
	if (!FBox2D(Scenario->BoundsMin + FVector2D(Radius), Scenario->BoundsMax - FVector2D(Radius))
	         .IsInsideOrOn(FVector2D(Ground)))
	{
		return false;
	}
	for (const FVector& Castle : {Scenario->AllyCastleGround, Scenario->EnemyCastleGround})
	{
		if (FVector::DistSquared2D(Ground, Castle) < FMath::Square(Radius + 142 + 2))
		{
			return false;
		}
	}
	FCollisionObjectQueryParams Objects;
	for (ECollisionChannel Channel : {ECC_WorldStatic, ECC_WorldDynamic, LSCollisionChannels::Unit,
	                                  LSCollisionChannels::Building, LSCollisionChannels::PathObstacle})
	{
		Objects.AddObjectTypesToQuery(Channel);
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSEnemyPlacement), false, Scenario);
	return !Scenario->GetWorld()->OverlapAnyTestByObjectType(Ground + FVector(0, 0, 90), FQuat::Identity, Objects,
	                                                         FCollisionShape::MakeCapsule(Radius, 88), Query);
}
bool OverlapsEnemy(const FLSAutoBattlePlacement& A, const FLSAutoBattlePlacement& B)
{
	return FMath::Abs(A.Position.Z - B.Position.Z) < 176 &&
	       FVector::DistSquared2D(A.Position, B.Position) <
	           FMath::Square(EnemyRadius(A.Type) + EnemyRadius(B.Type) + 2);
}
ALSAutoBattleEnemyMarker* SpawnEnemyMarker(ALSAutoBattleScenario* Scenario, ELSTroopType Type, const FVector& Ground)
{
	FActorSpawnParameters Parameters;
	Parameters.OverrideLevel = Scenario->GetLevel();
	Parameters.ObjectFlags |= RF_Transactional;
	auto* Marker = Scenario->GetWorld()->SpawnActor<ALSAutoBattleEnemyMarker>(
	    ALSAutoBattleEnemyMarker::StaticClass(), Ground, FRotator::ZeroRotator, Parameters);
	if (Marker)
	{
		Marker->Modify();
		Marker->Scenario = Scenario;
		Marker->TroopType = Type;
		Marker->SetActorLabel(TEXT("EnemyPlacement_") + ALSTroopUnit::GetStatsRowName(Type).ToString());
		Marker->RerunConstructionScripts();
	}
	return Marker;
}
} // namespace
#endif

void ALSAutoBattleScenario::LoadEnemyMarkers()
{
#if WITH_EDITOR
	if (!CanEditEnemyPlacement(this))
	{
		return;
	}
	// 기존 마커를 삭제하지 않고 거부하여 적용 전 편집을 덮어쓰지 않는다.
	if (!FindEnemyMarkers(this).IsEmpty())
	{
		PlacementToolStatus = TEXT("마커가 이미 있습니다. 현재 편집을 Apply하거나 Hide 후 다시 Load하세요.");
		return;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("Landship", "LoadEnemyMarkers", "적 배치 마커 불러오기"));
	Modify();
	TArray<ALSAutoBattleEnemyMarker*> Created;
	for (const FLSAutoBattlePlacement& Placement : EnemyFormation)
	{
		auto* Marker = SpawnEnemyMarker(this, Placement.Type, Placement.Position);
		if (!Marker)
		{
			for (auto* Existing : Created)
			{
				GetWorld()->EditorDestroyActor(Existing, true);
			}
			PlacementToolStatus = TEXT("마커 생성 실패. 저장된 편성은 유지되었습니다.");
			return;
		}
		Created.Add(Marker);
	}
	bEnemyMarkersLoaded = true;
	PlacementToolStatus = FString::Printf(
	    TEXT("적 %d기 로드. 이동 / Alt+드래그 복제 / Delete 삭제 / Details 병종 변경 후 Apply하세요."), Created.Num());
	GEditor->RedrawLevelEditingViewports();
#endif
}

void ALSAutoBattleScenario::AddEnemyMarker()
{
#if WITH_EDITOR
	if (!CanEditEnemyPlacement(this))
	{
		return;
	}
	const TArray<ALSAutoBattleEnemyMarker*> Markers = FindEnemyMarkers(this);
	if (!bEnemyMarkersLoaded && Markers.IsEmpty() && !EnemyFormation.IsEmpty())
	{
		PlacementToolStatus = TEXT("먼저 Load Saved Formation으로 기존 편성을 불러오세요.");
		return;
	}
	if (Markers.Num() >= 100 || ALSTroopUnit::GetStatsRowName(PlacementTroopType).IsNone())
	{
		PlacementToolStatus = TEXT("적 상한 또는 병종을 확인하세요.");
		return;
	}
	FVector Ground;
	bool bFound = false;
	// 새 마커가 기존 것 위에 겹쳐 생기지 않도록 가까운 빈 후보부터 찾는다.
	for (int32 Index = 0; Index < 401 && !bFound; ++Index)
	{
		const int32 Ring = Index == 0 ? 0 : (Index - 1) / 16 + 1;
		const double Angle = Index == 0 ? 0 : ((Index - 1) % 16) * 2 * PI / 16;
		const FVector Desired = PlacementGround + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Ring * 150;
		if (!ResolveEnemyGround(this, PlacementTroopType, Desired, Ground))
		{
			continue;
		}
		FLSAutoBattlePlacement Candidate;
		Candidate.Type = PlacementTroopType;
		Candidate.Position = Ground;
		bFound = true;
		for (const auto* Marker : Markers)
		{
			FLSAutoBattlePlacement Other;
			Other.Type = Marker->TroopType;
			Other.Position = Marker->GetActorLocation();
			if (OverlapsEnemy(Candidate, Other))
			{
				bFound = false;
				break;
			}
		}
	}
	if (!bFound)
	{
		PlacementToolStatus = TEXT("주변에 배치할 빈 지면이 없습니다. Placement Ground를 바꾸세요.");
		return;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("Landship", "AddEnemyMarker", "적 배치 마커 추가"));
	Modify();
	auto* Marker = SpawnEnemyMarker(this, PlacementTroopType, Ground);
	if (!Marker)
	{
		PlacementToolStatus = TEXT("마커 생성 실패.");
		return;
	}
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Marker, true, true);
	PlacementToolStatus =
	    TEXT("추가됨. W 이동 / Alt+드래그 복제 / Details 병종 변경. 시나리오를 선택하여 Apply하세요.");
	GEditor->RedrawLevelEditingViewports();
#endif
}

void ALSAutoBattleScenario::ApplyEnemyMarkers()
{
#if WITH_EDITOR
	if (!CanEditEnemyPlacement(this))
	{
		return;
	}
	const TArray<ALSAutoBattleEnemyMarker*> Markers = FindEnemyMarkers(this);
	if (Markers.IsEmpty() || Markers.Num() > 100)
	{
		PlacementToolStatus = TEXT("적은 1~100기여야 합니다. 저장된 편성은 유지됩니다.");
		return;
	}
	TArray<FLSAutoBattlePlacement> Candidate;
	for (const auto* Marker : Markers)
	{
		FLSAutoBattlePlacement Placement;
		Placement.Type = Marker->TroopType;
		if (Marker->GetLevel() != GetLevel() || !Marker->GetActorScale3D().Equals(FVector::OneVector) ||
		    !ResolveEnemyGround(this, Placement.Type, Marker->GetActorLocation(), Placement.Position))
		{
			PlacementToolStatus =
			    Marker->GetActorLabel() + TEXT(": 지면·전장 경계·성/장애물 겹침·스케일을 확인하세요. 기존 편성 유지.");
			return;
		}
		for (const auto& Other : Candidate)
		{
			if (OverlapsEnemy(Placement, Other))
			{
				PlacementToolStatus = Marker->GetActorLabel() + TEXT(": 병력이 겹칩니다. 기존 편성 유지.");
				return;
			}
		}
		Candidate.Add(Placement);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("Landship", "ApplyEnemyMarkers", "적 배치 편성 적용"));
	Modify();
	EnemyFormation = Candidate;
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		Markers[Index]->Modify();
		Markers[Index]->SetActorLocation(Candidate[Index].Position);
	}
	MarkPackageDirty();
	PlacementToolStatus =
	    FString::Printf(TEXT("적 %d기 적용 완료. Ctrl+S로 맵을 저장한 뒤 PIE를 실행하세요."), Candidate.Num());
	GEditor->RedrawLevelEditingViewports();
#endif
}

void ALSAutoBattleScenario::HideEnemyMarkers()
{
#if WITH_EDITOR
	if (!CanEditEnemyPlacement(this))
	{
		return;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("Landship", "HideEnemyMarkers", "적 배치 마커 숨기기"));
	Modify();
	for (auto* Marker : FindEnemyMarkers(this))
	{
		GEditor->SelectActor(Marker, false, false);
		GetWorld()->EditorDestroyActor(Marker, true);
	}
	bEnemyMarkersLoaded = false;
	PlacementToolStatus = TEXT("마커 제거됨. Apply하지 않은 편집은 반영되지 않았습니다. Ctrl+Z로 복구할 수 있습니다.");
	GEditor->RedrawLevelEditingViewports();
#endif
}
