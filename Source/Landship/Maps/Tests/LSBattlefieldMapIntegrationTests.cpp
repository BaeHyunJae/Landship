#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Maps/LSBattlefieldLayoutAuthoring.h"
#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Maps/LSBattlefieldCastleNavMesh.h"
#include "AIController.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HUD/LSBattleHUDWidget.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Obstacles/LSTurret.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSOutpost.h"
#include "Enemies/Tests/LSEnemyVisionTestLibrary.h"
#include "Enemies/Tests/LSEnemySupportTestAccess.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Match/LSGameState.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/LSNavigationSetup.h"
#include "Navigation/LSMineAvoidance.h"
#include "Navigation/LSMineAwareMovementComponent.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSMineHazardSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"

namespace
{
constexpr float CastleNavigationRadius = 580.0f;
const TCHAR* BattlefieldMap = TEXT("/Game/Landship/Maps/L_BattlefieldPrototype");

template <typename T> T* FindOnlyActor(UWorld* World)
{
	T* Found = nullptr;
	for (TActorIterator<T> It(World); It; ++It)
	{
		if (Found) return nullptr;
		Found = *It;
	}
	return Found;
}

int32 CountActors(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It) ++Count;
	return Count;
}

template <typename T> T* FindNamedActor(UWorld* World, FName Name)
{
	for (TActorIterator<T> It(World); It; ++It)
		if (It->GetFName() == Name) return *It;
	return nullptr;
}

FString MaterialValues(UMaterialInterface* Material)
{
	UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
	if (!Dynamic) return GetPathNameSafe(Material);
	FString Value = GetPathNameSafe(Dynamic->Parent);
	TArray<FMaterialParameterInfo> Info;
	TArray<FGuid> Ids;
	Dynamic->GetAllScalarParameterInfo(Info, Ids);
	for (const FMaterialParameterInfo& Parameter : Info)
	{
		float Scalar = 0;
		if (Dynamic->GetScalarParameterValue(Parameter, Scalar))
			Value += FString::Printf(TEXT("|S:%s:%d:%d=%.9g"), *Parameter.Name.ToString(), static_cast<int32>(Parameter.Association), Parameter.Index, Scalar);
	}
	Info.Reset(); Ids.Reset();
	Dynamic->GetAllVectorParameterInfo(Info, Ids);
	for (const FMaterialParameterInfo& Parameter : Info)
	{
		FLinearColor Vector;
		if (Dynamic->GetVectorParameterValue(Parameter, Vector))
			Value += FString::Printf(TEXT("|V:%s:%d:%d=%.9g,%.9g,%.9g,%.9g"), *Parameter.Name.ToString(), static_cast<int32>(Parameter.Association), Parameter.Index, Vector.R, Vector.G, Vector.B, Vector.A);
	}
	Info.Reset(); Ids.Reset();
	Dynamic->GetAllTextureParameterInfo(Info, Ids);
	for (const FMaterialParameterInfo& Parameter : Info)
	{
		UTexture* Texture = nullptr;
		if (Dynamic->GetTextureParameterValue(Parameter, Texture))
			Value += FString::Printf(TEXT("|T:%s:%d:%d=%s"), *Parameter.Name.ToString(), static_cast<int32>(Parameter.Association), Parameter.Index, *GetPathNameSafe(Texture));
	}
	return Value;
}

TMap<FString, FString> EditableValues(AActor* Actor)
{
	TMap<FString, FString> Values;
	TArray<UObject*> Objects{Actor};
	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components) Objects.Add(Component);
	for (UObject* Object : Objects)
	{
		const FString Prefix = Object == Actor ? TEXT("Actor") : Object->GetName();
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			const FName Name = Property->GetFName();
			if (Object->IsA<USceneComponent>() &&
				(Name == TEXT("RelativeLocation") || Name == TEXT("RelativeRotation") || Name == TEXT("RelativeScale3D"))) continue;
			FString Value;
			if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
				Value = TextProperty->GetPropertyValue_InContainer(Object).ToString();
			else if (Name == TEXT("OverrideMaterials") && Object->IsA<UMeshComponent>())
			{
				// 재구성 때 바뀌는 MID 이름 대신 부모 재질과 실제 매개변수 값을 비교한다.
				const FArrayProperty* Array = CastFieldChecked<FArrayProperty>(Property);
				FScriptArrayHelper Materials(Array, Array->ContainerPtrToValuePtr<void>(Object));
				const FObjectPropertyBase* Inner = CastFieldChecked<FObjectPropertyBase>(Array->Inner);
				for (int32 Index = 0; Index < Materials.Num(); ++Index)
					Value += FString::Printf(TEXT("[%d:%s]"), Index, *MaterialValues(Cast<UMaterialInterface>(Inner->GetObjectPropertyValue(Materials.GetRawPtr(Index)))));
			}
			else Property->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
			Values.Add(Prefix + TEXT(".") + Property->GetName(), Value);
		}
	}
	return Values;
}

struct FLSBattlefieldActorSnapshot
{
	FString ClassPath;
	FTransform Transform;
	TMap<FString, FString> Values;
};

using FLSBattlefieldSnapshot = TMap<FName, FLSBattlefieldActorSnapshot>;

FLSBattlefieldSnapshot CaptureRegistry(ALSBattlefieldLayoutAuthoring* Layout)
{
	FLSBattlefieldSnapshot Snapshot;
	for (const FLSBattlefieldLayoutActorEntry& Entry : Layout->Registry)
	{
		if (!IsValid(Entry.Actor)) continue;
		Snapshot.Add(Entry.ActorId, {Entry.Actor->GetClass()->GetPathName(), Entry.Actor->GetActorTransform(), EditableValues(Entry.Actor)});
	}
	return Snapshot;
}

void CheckRegistry(FAutomationTestBase& Test, ALSBattlefieldLayoutAuthoring* Layout,
	const FLSBattlefieldSnapshot& Expected, const FLSBattlefieldLayoutPreset* Preset = nullptr)
{
	Test.TestEqual(TEXT("Registry count preserved"), Layout->Registry.Num(), Expected.Num());
	for (const FLSBattlefieldLayoutActorEntry& Entry : Layout->Registry)
	{
		const FLSBattlefieldActorSnapshot* Before = Expected.Find(Entry.ActorId);
		if (!Test.TestTrue(Entry.ActorId.ToString() + TEXT(" retains valid actor and ID"), Before && IsValid(Entry.Actor))) continue;
		const FTransform* Placement = Preset ? Preset->Placements.Find(Entry.ActorId) : &Before->Transform;
		Test.TestTrue(Entry.ActorId.ToString() + TEXT(" full transform"), Placement && Entry.Actor->GetActorTransform().Equals(*Placement, 0.001));
		Test.TestEqual(Entry.ActorId.ToString() + TEXT(" class"), Entry.Actor->GetClass()->GetPathName(), Before->ClassPath);
		const TMap<FString, FString> Values = EditableValues(Entry.Actor);
		Test.TestEqual(Entry.ActorId.ToString() + TEXT(" editable property count"), Values.Num(), Before->Values.Num());
		for (const TPair<FString, FString>& Value : Before->Values)
		{
			const FString* Actual = Values.Find(Value.Key);
			Test.TestTrue(Entry.ActorId.ToString() + TEXT(" preserves ") + Value.Key, Actual && *Actual == Value.Value);
		}
	}
}

void CheckPresetDefinitions(FAutomationTestBase& Test, const ALSBattlefieldLayoutAuthoring* Layout)
{
	Test.TestEqual(TEXT("Only complete V0 is selectable during this iteration"), Layout->Presets.Num(), 1);
	const FLSBattlefieldLayoutPreset* Base = Layout->Presets.FindByPredicate(
		[](const FLSBattlefieldLayoutPreset& Candidate) { return Candidate.PresetId == TEXT("V0"); });
	if (Test.TestNotNull(TEXT("V0 definition exists"), Base))
		Test.TestEqual(TEXT("V0 covers every registry actor"), Base->Placements.Num(), Layout->Registry.Num());
}

bool ValidateMap(FAutomationTestBase& Test, UWorld* World)
{
	bool bValid = true;
	auto Validate = [&Test, &bValid](UObject* Object, const TCHAR* Name, bool bReportsValid = true)
	{
		if (!Test.TestNotNull(Name, Object)) { bValid = false; return; }
		FDataValidationContext Context;
		const EDataValidationResult Result = Object->IsDataValid(Context);
		bValid &= Test.TestTrue(Name, bReportsValid ? Result == EDataValidationResult::Valid : Result != EDataValidationResult::Invalid);
		bValid &= Test.TestEqual(FString(Name) + TEXT(" validation errors"), Context.GetNumErrors(), 0u);
	};
	Validate(FindOnlyActor<ALSBattlefieldSettings>(World), TEXT("Unique battlefield settings"));
	Validate(FindOnlyActor<ALSBattlefieldSetup>(World), TEXT("Unique battlefield setup"));
	Validate(FindOnlyActor<ALSBattlefieldLayoutAuthoring>(World), TEXT("Unique layout authoring"));
	Validate(FindOnlyActor<ALSBattlefieldEncounterCoordinator>(World), TEXT("Unique regional encounter coordinator"));
	for (TActorIterator<ALSNavigationSetup> It(World); It; ++It)
		bValid &= Test.TestFalse(TEXT("Legacy single-agent navigation setup absent"), true);
	int32 Guards = 0;
	int32 Assaults = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (ULSEnemyBehaviorComponent* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>())
		{
			Validate(*It, *It->GetActorLabel(), false);
			// 적 행동 검사는 오류가 없으면 부모의 NotValidated를 그대로 반환한다.
			Validate(Behavior, *It->GetActorLabel(), false);
			if (It->IsA<ALSEnemyTroop>()) Behavior->Role == ELSEnemyRole::Guard ? ++Guards : ++Assaults;
		}
	}
	bValid &= Test.TestEqual(TEXT("186 authored Guard troops"), Guards, 186);
	bValid &= Test.TestEqual(TEXT("84 regionally dormant Assault troops"), Assaults, 84);
	return bValid;
}

struct FLSBattlefieldMapTestState
{
	FAutomationTestBase* Test = nullptr;
	FString OriginalFilename;
	FString SourceFilename;
	FMD5Hash SourceHash;
	FString MountRoot;
	FString Directory;
	FString CopyFilename;
	TWeakObjectPtr<UWorld> EditorCopy;
	bool bReady = false;
};

struct FLSBattlefieldObstacleObservation
{
	bool bMineDestroyed = false;
	bool bMineDisarmed = false;
	bool bBarricadeDestroyed = false;
	bool bBarricadeCollisionDisabled = false;
	FName BarricadeName;
	FName MineName;
	FName ChainAName, ChainBName;
	bool bChainAExploded = false, bChainBExploded = false;
};

class FLSBattlefieldCleanupCommand : public IAutomationLatentCommand
{
public:
	explicit FLSBattlefieldCleanupCommand(TSharedRef<FLSBattlefieldMapTestState> InState) : State(InState) {}
	virtual bool Update() override
	{
		if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
		{
			GEditor->RequestEndPlayMap();
			if (GetCurrentRunTime() < 30) return false;
			State->Test->AddError(TEXT("PIE did not end; test copy retained for diagnosis."));
			return true;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World && (World == State->EditorCopy.Get() || World->GetOutermost()->GetName().StartsWith(State->MountRoot))) World->GetOutermost()->SetDirtyFlag(false);
		const bool bRestored = State->OriginalFilename.IsEmpty()
			? FAutomationEditorCommonUtils::CreateNewMap() != nullptr
			: FEditorFileUtils::LoadMap(State->OriginalFilename, false, false);
		State->Test->TestTrue(TEXT("Restore previous clean editor map"), bRestored);
		State->Test->TestTrue(TEXT("Source map bytes unchanged"), FMD5Hash::HashFile(*State->SourceFilename) == State->SourceHash);
		if (bRestored)
		{
			FPackageName::UnRegisterMountPoint(State->MountRoot, State->Directory);
			const FString TestRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/BattlefieldMap"));
			if (State->Test->TestTrue(TEXT("Temporary directory remains inside automation output"), FPaths::IsUnderDirectory(State->Directory, TestRoot)))
				State->Test->TestTrue(TEXT("Remove only this run's temporary map directory"), IFileManager::Get().DeleteDirectory(*State->Directory, false, true));
		}
		return true;
	}
private:
	TSharedRef<FLSBattlefieldMapTestState> State;
};

void ReportNavigationState(FAutomationTestBase& Test, UWorld* World, const TCHAR* Stage)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) { Test.AddInfo(FString::Printf(TEXT("Navigation %s: no navigation system"), Stage)); return; }
	Test.AddInfo(FString::Printf(TEXT("Navigation %s: dirty=%d locked=%d building=%d running=%d remaining=%d"), Stage,
		Nav->HasDirtyAreasQueued(), Nav->IsNavigationBuildingLocked(), Nav->IsNavigationBuildInProgress(), Nav->GetNumRunningBuildTasks(), Nav->GetNumRemainingBuildTasks()));
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		Test.AddInfo(FString::Printf(TEXT("Navigation %s volume=%s agents=0x%08x bounds=%s"), Stage,
			*It->GetActorLabel(), It->SupportedAgents.GetAgentBits(), *It->GetComponentsBoundingBox().ToString()));
	for (const FNavDataConfig& Profile : Nav->GetSupportedAgents())
	{
		const ANavigationData* Data = Nav->GetNavDataForProps(Profile);
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
		int32 PopulatedTiles = 0;
		if (Recast)
		{
			TArray<FNavTileRef> Tiles;
			Recast->GetAllNavMeshTiles(Tiles);
			for (const FNavTileRef Tile : Tiles) if (Recast->GetNavMeshTileBounds(Tile).IsValid) ++PopulatedTiles;
		}
		FNavLocation Origin, Junction;
		const bool bOrigin = Data && Nav->ProjectPointToNavigation(FVector::ZeroVector, Origin, FVector(150, 150, 300), Data);
		const bool bJunction = Data && Nav->ProjectPointToNavigation(FVector(2500, 0, 0), Junction, FVector(150, 150, 300), Data);
		Test.AddInfo(FString::Printf(TEXT("Navigation %s profile=%s data=%s radius=%.0f height=%.0f runtime=%d validMesh=%d populatedTiles=%d bounds=%s projectS=%d projectJ0=%d"), Stage,
			*Profile.Name.ToString(), *GetNameSafe(Data), Profile.AgentRadius, Profile.AgentHeight, Data ? static_cast<int32>(Data->GetRuntimeGenerationMode()) : -1,
			Recast && Recast->HasValidNavmesh(), PopulatedTiles, Data ? *Data->GetBounds().ToString() : TEXT("none"), bOrigin, bJunction));
	}
}

bool CompletePath(UNavigationSystemV1* Nav, const ANavigationData* Data, const AActor* Owner,
	const FVector& Start, const FVector& End, FVector* OutEnd = nullptr, FNavPathSharedPtr* OutPath = nullptr, FString* OutFailure = nullptr)
{
	FNavLocation From, To;
	const FVector Extent(150, 150, 300);
	const bool bFrom = Nav->ProjectPointToNavigation(Start, From, Extent, Data);
	const bool bTo = Nav->ProjectPointToNavigation(End, To, Extent, Data);
	if (!bFrom || !bTo)
	{
		if (OutFailure) *OutFailure = FString::Printf(TEXT("projection start=%d %s end=%d %s navBounds=%s"), bFrom, *Start.ToString(), bTo, *End.ToString(), *Data->GetBounds().ToString());
		return false;
	}
	if (FVector::Dist2D(Start, From.Location) > 150 || FVector::Dist2D(End, To.Location) > 150)
	{
		if (OutFailure) *OutFailure = FString::Printf(TEXT("projection drift start=%.1f end=%.1f"), FVector::Dist2D(Start, From.Location), FVector::Dist2D(End, To.Location));
		return false;
	}
	const FNavAgentProperties& Agent = Data->GetNavAgentProperties();
	FPathFindingQuery Query(Owner, *Data, From.Location, To.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Path = Nav->FindPathSync(Agent, Query);
	if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
	{
		if (OutFailure) *OutFailure = FString::Printf(TEXT("path result=%d valid=%d partial=%d from=%s to=%s"), static_cast<int32>(Path.Result), Path.Path.IsValid(), Path.Path.IsValid() && Path.Path->IsPartial(), *From.Location.ToString(), *To.Location.ToString());
		return false;
	}
	if (OutEnd) *OutEnd = To.Location;
	if (OutPath) *OutPath = Path.Path;
	return true;
}

bool FindBarricadeCrossing(UNavigationSystemV1* Nav, const ANavigationData* Data, ALSBarricade* Barricade,
	float GroundZ, FVector& OutFrom, FVector& OutTo, FString& Details)
{
	UBoxComponent* Body = Cast<UBoxComponent>(Barricade->GetObstacleBody());
	if (!Body) { Details = TEXT("Barricade obstacle body is not a box"); return false; }
	const FVector Extent = Body->GetScaledBoxExtent();
	const FVector XAxis = Body->GetForwardVector(), YAxis = Body->GetRightVector();
	const FVector ShortAxis = Extent.X <= Extent.Y ? XAxis : YAxis;
	FVector Center = Body->GetComponentLocation();
	Center.Z = GroundZ;
	Details = FString::Printf(TEXT("bodyBounds=%s scaledHalfExtent=%s yaw=%.1f profile=%s"), *Body->Bounds.GetBox().ToString(), *Extent.ToString(), Body->GetComponentRotation().Yaw, *Data->GetConfig().Name.ToString());
	for (const float Angle : {0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 75.0f, -75.0f})
	{
		for (const float EndpointMargin : {150.0f, 100.0f, 50.0f, 10.0f})
		{
			const FVector Direction = ShortAxis.RotateAngleAxis(Angle, FVector::UpVector);
			const float Support = FMath::Abs(FVector::DotProduct(Direction, XAxis)) * Extent.X + FMath::Abs(FVector::DotProduct(Direction, YAxis)) * Extent.Y;
			const FVector Offset = Direction * (Support + Data->GetNavAgentProperties().AgentRadius + EndpointMargin);
			const FVector Start = Center - Offset, End = Center + Offset;
			FNavLocation From, To;
			const bool bFrom = Nav->ProjectPointToNavigation(Start, From, FVector(50, 50, 300), Data);
			const bool bTo = Nav->ProjectPointToNavigation(End, To, FVector(50, 50, 300), Data);
			Details += FString::Printf(TEXT(" | angle=%.0f margin=%.0f start=%s project=%d:%s end=%s project=%d:%s"), Angle, EndpointMargin, *Start.ToString(), bFrom, bFrom ? *From.Location.ToString() : TEXT("none"), *End.ToString(), bTo, bTo ? *To.Location.ToString() : TEXT("none"));
			if (!bFrom || !bTo) continue;
			FVector NavHit;
			FHitResult BodyHit;
			if (!Body->LineTraceComponent(BodyHit, From.Location, To.Location, FCollisionQueryParams(SCENE_QUERY_STAT(LSBarricadeCrossingBody), false)) ||
				!Data->Raycast(From.Location, To.Location, NavHit, Data->GetDefaultQueryFilter(), nullptr)) continue;
			const ALSCastle* Castle = FMath::IsNearlyEqual(Data->GetConfig().AgentRadius, CastleNavigationRadius) ? FindOnlyActor<ALSCastle>(Barricade->GetWorld()) : nullptr;
			const float BodyRadius = Castle ? Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() : Data->GetNavAgentProperties().AgentRadius;
			const float BodyHalfHeight = Castle ? Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : Data->GetNavAgentProperties().AgentHeight * 0.5f;
			const float ClearanceRadius = BodyRadius + 50;
			const float HalfHeight = FMath::Max(ClearanceRadius, BodyHalfHeight);
			const FVector HeightOffset(0, 0, HalfHeight);
			const FCollisionShape AgentBody = FCollisionShape::MakeCapsule(ClearanceRadius, HalfHeight);
			bool bOtherGeometry = false;
			for (TActorIterator<AActor> It(Barricade->GetWorld()); It && !bOtherGeometry; ++It)
			{
				if (*It == Barricade || It->IsA<APawn>()) continue;
				TInlineComponentArray<UPrimitiveComponent*> Components(*It);
				for (UPrimitiveComponent* Component : Components)
				{
					if (!Component->CanEverAffectNavigation() || !Component->IsNavigationRelevant() ||
						Component->GetCollisionObjectType() == LSCollisionChannels::Ground) continue;
					FHitResult OtherHit;
					if (Component->SweepComponent(OtherHit, From.Location + HeightOffset, To.Location + HeightOffset, FQuat::Identity, AgentBody))
					{
						Details += FString::Printf(TEXT(" rejectedOtherNavGeometry=%s hit=%s clearanceRadius=%.1f"), *Component->GetPathName(), *OtherHit.Location.ToString(), ClearanceRadius);
						bOtherGeometry = true;
						break;
					}
				}
			}
			if (bOtherGeometry) continue;
			OutFrom = From.Location;
			OutTo = To.Location;
			return true;
		}
	}
	return false;
}

TArray<FVector> BattlefieldCenterline(int32 Lane)
{
	const FVector S(0, 0, 0), J0(2500, 0, 0), J2(18200, 0, 0), PreCore(18500, 1800, 0);
	switch (Lane)
	{
	case 0: return {S, J0, FVector(3000, -6300, 0), FVector(4000, -6300, 0), FVector(4600, -7300, 0), FVector(10500, -6300, 0), FVector(15600, -6300, 0), J2, PreCore, FVector(20000, 1800, 0)};
	case 1: return {S, J0, FVector(4500, 0, 0), FVector(10500, 0, 0), FVector(16100, 0, 0), J2, PreCore, FVector(20000, 1800, 0)};
	case 2: return {S, J0, FVector(3000, 4600, 0), FVector(4000, 4600, 0), FVector(4500, 5100, 0), FVector(10500, 4600, 0), FVector(12500, 5100, 0), FVector(13050, 4600, 0), FVector(15600, 4600, 0), J2, PreCore, FVector(20000, 1800, 0)};
	case 3: return {FVector(10500, -6300, 0), FVector(10500, 0, 0), FVector(10500, 4600, 0)};
	default: return {};
	}
}

bool CheckLanePath(FAutomationTestBase& Test, const FString& Label, const FNavPathSharedPtr& Path,
	const TArray<FVector>& Centerline, const FVector& From, const FVector& To, bool bCheckMacroLength = true)
{
	if (!Test.TestTrue(Label + TEXT(" has a specified lane centerline"), Centerline.Num() >= 2)) return false;
	if (!Test.TestTrue(Label + TEXT(" contains a complete path"), Path.IsValid() && Path->IsValid() && !Path->IsPartial() && Path->GetPathPoints().Num() >= 2)) return false;
	double MaxDistanceSquared = 0;
	double Length = 0;
	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector Start(Points[Index - 1].Location.X, Points[Index - 1].Location.Y, 0);
		const FVector End(Points[Index].Location.X, Points[Index].Location.Y, 0);
		const double SegmentLength = FVector::Distance(Start, End);
		Length += SegmentLength;
		const int32 Samples = FMath::Max(1, FMath::CeilToInt(SegmentLength / 200));
		for (int32 Sample = 0; Sample <= Samples; ++Sample)
		{
			const FVector Point = FMath::Lerp(Start, End, static_cast<double>(Sample) / Samples);
			double DistanceSquared = TNumericLimits<double>::Max();
			for (int32 Segment = 1; Segment < Centerline.Num(); ++Segment)
				DistanceSquared = FMath::Min(DistanceSquared, static_cast<double>(FMath::PointDistToSegmentSquared(Point, Centerline[Segment - 1], Centerline[Segment])));
			MaxDistanceSquared = FMath::Max(MaxDistanceSquared, DistanceSquared);
		}
	}
	// 폭 30m의 해당 통로만 허용하며 격자 100cm와 Nav 투영 150cm의 오차를 더한다.
	const bool bInside = Test.TestTrue(FString::Printf(TEXT("%s stays within lane corridor (max %.1fcm, limit 1750cm)"), *Label, FMath::Sqrt(MaxDistanceSquared)), MaxDistanceSquared <= FMath::Square(1750.0));
	const double LengthLimit = FVector::Dist2D(From, To) * 1.3 + 300;
	const bool bShort = !bCheckMacroLength || Test.TestTrue(FString::Printf(TEXT("%s length %.1fcm <= %.1fcm including endpoint projection"), *Label, Length, LengthLimit), Length <= LengthLimit);
	return bInside && bShort;
}

bool CheckLocalBarricadeDetour(FAutomationTestBase& Test, const FString& Label, const FNavPathSharedPtr& Path,
	ALSBarricade* Barricade, const ANavigationData* Data, const TArray<FVector>& Centerline, const FVector& From, const FVector& To)
{
	if (!CheckLanePath(Test, Label, Path, Centerline, From, To, false)) return false;
	const FBox Body = Barricade->GetObstacleBody()->Bounds.GetBox();
	FBox2D Neighborhood(FVector2D(Body.Min.X, Body.Min.Y), FVector2D(Body.Max.X, Body.Max.Y));
	Neighborhood += FVector2D(From.X, From.Y);
	Neighborhood += FVector2D(To.X, To.Y);
	const FVector HalfExtent = Body.GetExtent();
	const double Margin = FMath::Max(HalfExtent.X, HalfExtent.Y) + Data->GetNavAgentProperties().AgentRadius * 2 + 150;
	Neighborhood = Neighborhood.ExpandBy(Margin);
	FBox2D Actual(ForceInit);
	FString Points;
	bool bLocal = true;
	for (const FNavPathPoint& Point : Path->GetPathPoints())
	{
		const FVector2D XY(Point.Location.X, Point.Location.Y);
		Actual += XY;
		bLocal &= Neighborhood.IsInsideOrOn(XY);
		Points += Point.Location.ToString() + TEXT(";");
	}
	Test.AddInfo(FString::Printf(TEXT("%s actualBounds=%s localBodyNeighborhood=%s points=%s"), *Label, *Actual.ToString(), *Neighborhood.ToString(), *Points));
	return Test.TestTrue(Label + TEXT(" remains in the barricade and endpoint neighborhood"), bLocal);
}

class FLSBattlefieldPIECommand : public IAutomationLatentCommand
{
public:
	FLSBattlefieldPIECommand(TSharedRef<FLSBattlefieldMapTestState> InState, FName InPreset, bool bInDefeat = false)
		: State(InState), Preset(InPreset), bDefeat(bInDefeat) {}
	virtual ~FLSBattlefieldPIECommand() override { RemoveObstacleObserver(); }
	virtual bool Update() override
	{
		if (!State->bReady) return true;
		FAutomationTestBase& Test = *State->Test;
		if (Phase == 7)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()) return WaitOrFail(TEXT("End PIE before next preset"), 30);
			return true;
		}
		if (Phase == 0)
		{
			if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()) return WaitOrFail(TEXT("Previous PIE must end"), 30);
			ALSBattlefieldLayoutAuthoring* Layout = FindOnlyActor<ALSBattlefieldLayoutAuthoring>(GEditor->GetEditorWorldContext().World());
			if (!Test.TestNotNull(TEXT("Editor layout before PIE"), Layout)) return Finish();
			Layout->SelectedPreset = Preset;
			if (!Test.TestTrue(TEXT("Preset can apply outside PIE"), Layout->GetConfigurationError().IsEmpty())) return Finish();
			Layout->ApplySelectedPreset();
			if (!ValidateMap(Test, Layout->GetWorld())) return Finish();
			for (const FLSBattlefieldLayoutActorEntry& Entry : Layout->Registry) RegistryNames.Add(Entry.ActorId, Entry.Actor->GetFName());
			AdvanceTo(19);
			return false;
		}
		if (Phase == 19)
		{
			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(EditorWorld);
			bool bReady = Nav && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(EditorWorld) && Nav->GetSupportedAgents().Num() == 2;
			if (bReady)
				for (const FNavDataConfig& Profile : Nav->GetSupportedAgents())
				{
					const ANavigationData* Data = Nav->GetNavDataForProps(Profile);
					const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
					bool bHasPopulatedTile = false;
					if (Recast)
					{
						TArray<FNavTileRef> Tiles;
						Recast->GetAllNavMeshTiles(Tiles);
						for (const FNavTileRef Tile : Tiles)
							if (Recast->GetNavMeshTileBounds(Tile).IsValid) { bHasPopulatedTile = true; break; }
					}
					FNavLocation Origin, Junction;
					bReady &= bHasPopulatedTile && Nav->ProjectPointToNavigation(FVector::ZeroVector, Origin, FVector(150, 150, 300), Data)
						&& Nav->ProjectPointToNavigation(FVector(2500, 0, 0), Junction, FVector(150, 150, 300), Data);
				}
			if (!bReady)
			{
				if (FPlatformTime::Seconds() - PhaseStarted < 60) return false;
				ReportNavigationState(Test, EditorWorld, TEXT("Editor pre-PIE readiness timeout"));
				return WaitOrFail(TEXT("Editor navigation must finish and both profiles must project S/J0 before PIE"), 60);
			}
			ReportNavigationState(Test, EditorWorld, TEXT("Editor ready before PIE"));
			FRequestPlaySessionParams Params;
			Params.bAllowOnlineSubsystem = false;
			Starter = MakeUnique<FStartPIEForAutomationCommand>(Params);
			AdvanceTo(1);
		}
		if (Phase == 1)
		{
			if (!Starter->Update()) return false;
			Starter.Reset();
			World = GEditor->PlayWorld;
			if (!Test.TestTrue(TEXT("Real PIE world initialized"), World.IsValid() && World->WorldType == EWorldType::PIE && World->HasBegunPlay())) return Finish();
			Setup = FindOnlyActor<ALSBattlefieldSetup>(World.Get());
			GameState = World->GetGameState<ALSGameState>();
			Controller = Cast<ALSPlayerController>(World->GetFirstPlayerController());
			Castle = Setup.IsValid() ? Cast<ALSCastle>(Setup->PlayerCastle) : nullptr;
			if (!Test.TestTrue(TEXT("Map has configured runtime castle, Core, controller and GameState"),
				Setup.IsValid() && IsValid(Setup->CoreOutpost) && Castle.IsValid() && Controller.IsValid() && GameState.IsValid())) return Finish();
			Test.TestTrue(TEXT("Battle outcome enabled by real setup"), GameState->IsBattleOutcomeEnabled());
			Test.TestEqual(TEXT("Fresh battle outcome"), GameState->GetBattleOutcome(), ELSBattleOutcome::InProgress);
			Test.TestTrue(TEXT("Configured castle alive"), ULSCombatTargetLibrary::IsCombatAlive(Castle.Get()));
			Test.TestTrue(TEXT("Configured Core alive"), ULSCombatTargetLibrary::IsCombatAlive(Setup->CoreOutpost));
			Home = Castle->GetActorLocation();
			HomeGround = Castle->GetNavAgentLocation();
			MeleeEnemy = FindNamedActor<ALSEnemyTroop>(World.Get(), RegistryNames.FindRef(TEXT("E_L1_GUARD_SPEARMAN_01")));
			if (!Test.TestTrue(TEXT("Named authored melee Guard available"), MeleeEnemy.IsValid() && MeleeEnemy->GetTroopType() == ELSTroopType::Spearman)) return Finish();
			// 복제 맵의 한 Guard만 공격을 유지해 큰 성의 실제 접근·반복 피해를 분리 관찰한다.
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
				if (*It != MeleeEnemy.Get())
					if (const auto* Faction = Cast<ILSFactionInterface>(*It); Faction && Faction->GetFaction() == ELSFaction::Enemy)
						if (ULSBasicAttackComponent* Attack = It->FindComponentByClass<ULSBasicAttackComponent>()) Attack->EndBattleActions();

			Advance();
		}
		if (!World.IsValid()) { Test.AddError(TEXT("PIE world ended during observation")); return Finish(); }
		if (Phase == 2)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
			if (Nav && !bReportedNavigation)
			{
				ReportNavigationState(Test, World.Get(), TEXT("PIE first check"));
				bReportedNavigation = true;
			}
			bool bBehaviorsReady = true;
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
				if (ULSEnemyBehaviorComponent* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>())
					bBehaviorsReady &= Behavior->GetState() != ELSEnemyState::WaitingForSetup;
			if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World.Get()) || !bBehaviorsReady) return WaitOrFail(TEXT("Navigation rebuild and enemy setup"), 60);
			ReportNavigationState(Test, World.Get(), TEXT("PIE queries begin"));
			Test.TestEqual(TEXT("Map-local navigation system selected before registration"), Nav->GetClass(), ULSBattlefieldNavigationSystem::StaticClass());
			const TArray<FNavDataConfig>& Supported = Nav->GetSupportedAgents();
			if (!Test.TestEqual(TEXT("Troop and castle profiles registered"), Supported.Num(), 2)) return Finish();
			const ANavigationData* Data = nullptr;
			const ANavigationData* TroopData = nullptr;
			for (const FNavDataConfig& Profile : Supported)
			{
				const ANavigationData* Registered = Nav->GetNavDataForProps(Profile);
				if (!Test.TestNotNull(TEXT("Supported profile resolves its registered NavData"), Registered)) continue;
				Test.TestEqual(TEXT("Registration key matches generation settings"), GetTypeHash(Registered->GetNavAgentProperties()), GetTypeHash(static_cast<const FNavAgentProperties&>(Registered->GetConfig())));
				Test.TestEqual(TEXT("Profile hash matches registration key"), GetTypeHash(static_cast<const FNavAgentProperties&>(Profile)), GetTypeHash(Registered->GetNavAgentProperties()));
				if (FMath::IsNearlyEqual(Profile.AgentRadius, CastleNavigationRadius))
				{
					Data = Registered;
					Test.TestEqual(TEXT("Castle navigation height"), Profile.AgentHeight, 1000.0f);
					Test.TestEqual(TEXT("Castle precision class selected before registration"), Registered->GetClass(), ALSBattlefieldCastleNavMesh::StaticClass());
					if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Registered))
					{
						Test.TestEqual(TEXT("Castle-only simplification error"), Recast->MaxSimplificationError, 0.5f);
						Test.TestEqual(TEXT("Castle cell size is10"), Recast->GetCellSize(ENavigationDataResolution::Default), 10.0f);
						Test.TestEqual(TEXT("Generated Castle radius stays580"), Recast->AgentRadius, CastleNavigationRadius);
					}
				}
				if (FMath::IsNearlyEqual(Profile.AgentRadius, 142.0f))
				{
					TroopData = Registered;
					Test.TestEqual(TEXT("Troop navigation height"), Profile.AgentHeight, 284.0f);
					Test.TestEqual(TEXT("Troops retain base Recast class"), Registered->GetClass(), ARecastNavMesh::StaticClass());
					if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Registered))
					{
						Test.TestEqual(TEXT("Troop simplification remains1.3"), Recast->MaxSimplificationError, 1.3f);
						Test.TestEqual(TEXT("Troop default cell size remains19"), Recast->GetCellSize(ENavigationDataResolution::Default), 19.0f);
					}
				}
			}
			if (!Test.TestTrue(TEXT("Distinct correctly sized NavData"), Data && TroopData && Data != TroopData)) return Finish();
			Test.TestTrue(TEXT("Actual 500cm castle matches castle NavData"), Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation()) == Data);

			CheckInitialConditions();
			if (!CheckFrontApproachIsolation(TroopData) || !CheckNativeMouthWidths() || !CheckIntendedCombatBays(Nav, Data)) return Finish();
			bool bBarricadesReady = true;
			for (const FName Id : BarricadeIds)
			{
				ALSBarricade* Barrier = FindNamedActor<ALSBarricade>(World.Get(), RegistryNames.FindRef(Id));
				FVector From, To;
				FString Details;
				const bool bCrossing = Barrier && FindBarricadeCrossing(Nav, TroopData, Barrier, Castle->GetNavAgentLocation().Z, From, To, Details);
				Test.AddInfo(Id.ToString() + TEXT(" crossing preflight: ") + Details);
				bBarricadesReady &= Test.TestTrue(Id.ToString() + TEXT(" has projected crossing through its physical body and blocked NavData"), bCrossing);
			}
			if (!bBarricadesReady) return Finish();
			for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
				if (It->GetFaction() == ELSFaction::Ally)
				{
					FLSOrder Hold;
					Hold.Kind = ELSOrderKind::Hold;
					It->Orders->SubmitOrder(Hold);
				}
			CheckClosedGates(Nav, Data);
			CheckClosedGates(Nav, TroopData);
			CheckEncounterDormancy();
			CheckVision();
			if (bDefeat) { ApplyTerminalDamage(); AdvanceTo(6); return false; }
			for (TActorIterator<ALSEnemyTroop> It(World.Get()); It; ++It)
			{
				It->Behavior->SetComponentTickEnabled(false);
				It->GetCharacterMovement()->StopMovementImmediately();
			}
			for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
				if (It->GetFaction() == ELSFaction::Ally && It->GetTroopType() == ELSTroopType::Spearman) { Worker = *It; break; }
			OtherMine = FindNamedActor<ALSMine>(World.Get(), RegistryNames.FindRef(TEXT("M_R1_02")));
			if (!Test.TestTrue(TEXT("Independent authored mine exists and is Armed before isolated gate checks"), OtherMine.IsValid() && OtherMine->IsArmed())) return Finish();
			FLSHealthSnapshot MineHealth;
			OtherMine->GetHealthComponent()->TryGetHealth(MineHealth);
			Test.AddInfo(FString::Printf(TEXT("Independent mine baseline id=M_R1_02 actor=%s state=%d health=%.1f position=%s"), *OtherMine->GetName(), static_cast<int32>(OtherMine->GetMineState()), MineHealth.CurrentHealth, *OtherMine->GetTriggerCenter().ToString()));
			InstallObstacleObserver();
			if (!Test.TestNotNull(TEXT("Authored allied worker opens actual gates"), Worker.Get()) || !BeginBarricadeAttack()) return Finish();
			AdvanceTo(8);
			return false;
		}
		if (Phase == 10)
		{
			FLSHealthSnapshot Health;
			Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
			if (Health.CurrentHealth >= EncounterHealth)
			{
				if (FPlatformTime::Seconds() - PhaseStarted >= 15)
					Test.AddInfo(FString::Printf(TEXT("Guard approach timeout actor=%s position=%s castle=%s state=%d tick=%d target=%s stopReason=%s distance=%.3f"),
						*MeleeEnemy->GetActorLabel(), *MeleeEnemy->GetActorLocation().ToString(), *Castle->GetActorLocation().ToString(), static_cast<int32>(MeleeEnemy->Behavior->GetState()),
						MeleeEnemy->Behavior->IsComponentTickEnabled(), *GetNameSafe(MeleeEnemy->Behavior->GetTarget()), *MeleeEnemy->Behavior->GetLastStopReason(), ULSCombatTargetLibrary::GetAttackDistance(MeleeEnemy.Get(), Castle.Get())));
				return WaitOrFail(TEXT("Isolated authored Guard reaches and damages large castle"), 15);
			}
			EncounterHealth = Health.CurrentHealth;
			++EncounterHits;
			if (EncounterHits < 2) return false;
			Test.TestTrue(TEXT("Actual melee Guard repeatedly damages the 500cm castle"), MeleeEnemy->BasicAttack->GetAttackTarget() == Castle.Get() && Health.CurrentHealth > 0);
			Test.TestTrue(TEXT("Melee Guard reaches actual body-boundary range"), ULSCombatTargetLibrary::GetAttackDistance(MeleeEnemy.Get(), Castle.Get()) <= MeleeEnemy->BasicAttack->GetAttackSettings().Range);
			MeleeEnemy->BasicAttack->EndBattleActions();
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
				if (ULSEnemyBehaviorComponent* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>()) Behavior->EndBattleActions();
			Castle->GetMovementComponent()->StopMovementImmediately();
			Castle->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
			Test.AddInfo(TEXT("Subsequent movement/production/obstacle checks isolate geometry with authored enemy attacks disabled; the preceding two melee hits used a live authored Guard."));
			if (!BeginGroundWeaponHit(ELSCastleWeapon::Shotgun, TEXT("E_L1_GUARD_SPEARMAN_02"))) return Finish();
			AdvanceTo(11);
			return false;
		}
		if (Phase == 11 || Phase == 13)
		{
			FLSHealthSnapshot Health;
			if (!WeaponTarget.IsValid() || !WeaponTarget->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health))
			{
				Test.AddError(TEXT("Ground-shot target vanished before health observation"));
				return Finish();
			}
			if (Health.CurrentHealth >= WeaponTargetHealth) return WaitOrFail(TEXT("Ordinary horizontal castle projectile hits actual troop body"), 5);
			Test.TestTrue(TEXT("Ground-aimed W/E applies real projectile damage at enlarged castle launch height"), Health.CurrentHealth < WeaponTargetHealth);
			if (Phase == 11) CheckWeaponDoesNotBlockHome(TEXT("Real W impact preserves castle navigation"));
			WeaponTarget->GetCharacterMovement()->StopMovementImmediately();
			WeaponTarget->SetActorLocation(WeaponTargetOriginal, false, nullptr, ETeleportType::TeleportPhysics);
			if (Phase == 11)
			{
				if (!BeginGroundWeaponHit(ELSCastleWeapon::Ballista, TEXT("E_L1_GUARD_SPEARMAN_03"))) return Finish();
				AdvanceTo(13);
				return false;
			}
			CheckSelectionIndependentWeapons();
			for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
				if (It->GetFaction() == ELSFaction::Ally)
				{
					FLSOrder Idle;
					It->Orders->SubmitOrder(Idle);
				}
			AdvanceTo(12);
			return false;
		}
		if (Phase == 12)
		{
			if (YieldTroop.IsValid())
			{
				FLSOrder Idle;
				YieldTroop->Orders->SubmitOrder(Idle);
			}
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
			const ANavigationData* Data = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
			FVector Destination = FVector::ZeroVector;
			const FVector Offsets[] = {FVector(1800, 0, 0), FVector(0, 1800, 0), FVector(0, -1800, 0), FVector(-1200, 0, 0)};
			FString PathFailure;
			const bool bHeadingPath = CompletePath(Nav, Data, Castle.Get(), Castle->GetNavAgentLocation(),
				HomeGround + Offsets[MovementRunIndex], &Destination, nullptr, &PathFailure);
			if (!Test.TestTrue(FString::Printf(TEXT("Production heading %d path from=%s homeGround=%s actor=%s: %s"),
				MovementRunIndex, *Castle->GetNavAgentLocation().ToString(), *HomeGround.ToString(),
				*Castle->GetActorLocation().ToString(), *PathFailure), bHeadingPath)) return Finish();
			MovementDestination = Destination;
			PrepareIdleYield(Destination);
			Controller->ApplySelection({Castle.Get()});
			if (!Test.TestTrue(TEXT("Castle accepts public controller move order"), Controller->IssueSelectionOrder(ELSOrderKind::Move, Destination, nullptr, false))) return Finish();
			const ELSTroopType Types[] = {ELSTroopType::Spearman, ELSTroopType::Archer, ELSTroopType::Cavalry, ELSTroopType::Spearman};
			Controller->ProduceTroop(Types[MovementRunIndex]);
			Test.TestEqual(TEXT("Production during active movement succeeds"), Controller->GetLastProductionResult(), ELSProductionResult::Ready);
			CheckProducedCorridor();
			AdvanceTo(3);
			return false;
		}
		if (Phase == 3)
		{
			if (YieldTroop.IsValid()) bSawYield |= YieldTroop->Orders->IsYieldingToCastle();
			if (Castle->GetVelocity().Size2D() > 5)
				MinimumForwardDot = FMath::Min(MinimumForwardDot, FVector::DotProduct(Castle->GetVelocity().GetSafeNormal2D(), Castle->GetActorForwardVector()));
			if (Castle->Orders->GetOrderKind() != ELSOrderKind::Idle) return WaitOrFail(TEXT("Castle actual outward movement"), 20);
			Test.TestTrue(TEXT("Castle progresses after moving production"), FVector::Dist2D(Home, Castle->GetActorLocation()) >= FVector::Dist2D(Home, MovementDestination) - Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() - 200);
			Test.TestTrue(TEXT("Castle outward movement succeeded"), Castle->Orders->DidLastMovementSucceed());
			Test.TestTrue(TEXT("Automatic movement never reverses against facing"), MinimumForwardDot >= -0.05);
			if (YieldTroop.IsValid())
			{
				Test.TestTrue(TEXT("Initially idle ally visibly yields to approaching castle"), bSawYield && FVector::Dist2D(YieldStart, YieldTroop->GetActorLocation()) > 100);
				Test.TestEqual(TEXT("Yield preserves player's Idle intent"), YieldTroop->Orders->GetOrderKind(), ELSOrderKind::Idle);
				Test.TestEqual(TEXT("Yield creates no queued order"), YieldTroop->Orders->GetQueuedCount(), 0);
				FLSOrder Hold;
				Hold.Kind = ELSOrderKind::Hold;
				Test.TestTrue(TEXT("Explicit player Hold replaces temporary yielding"), YieldTroop->Orders->SubmitOrder(Hold));
				Test.TestFalse(TEXT("New Hold immediately cancels yielding"), YieldTroop->Orders->IsYieldingToCastle());
			}
			if (!Test.TestTrue(TEXT("Castle accepts public return order"), Controller->IssueSelectionOrder(ELSOrderKind::Move, HomeGround, nullptr, false))) return Finish();
			Advance();
			return false;
		}
		if (Phase == 4)
		{
			if (Castle->Orders->GetOrderKind() != ELSOrderKind::Idle) return WaitOrFail(TEXT("Castle actual return movement"), 20);
			Test.TestTrue(TEXT("Castle returned near original position"), FVector::Dist2D(Home, Castle->GetActorLocation()) <= Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() + 200);
			Test.TestTrue(TEXT("Castle return movement succeeded"), Castle->Orders->DidLastMovementSucceed());
			++MovementRunIndex;
			if (MovementRunIndex < 4)
			{
				AdvanceTo(12);
				return false;
			}
			AdvanceTo(18);
			return false;
		}
		if (Phase == 18)
		{
			for (ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Archer, ELSTroopType::Cavalry})
				if (Controller->Production->GetRemainingCooldown(Type) > 0) return WaitOrFail(TEXT("Moving-production cooldown naturally expires before three-type census"), 15);
			if (Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun) > 0) return WaitOrFail(TEXT("First W cooldown naturally expires before repeat-shot regression"), 15);
			CheckWeaponDoesNotBlockHome(TEXT("Castle navigation before repeated W"));
			FLSCastleWeaponSettings Settings;
			if (!Test.TestTrue(TEXT("Repeated W uses actual configured projectile flight settings"), Castle->Weapons->GetResolvedSettings(ELSCastleWeapon::Shotgun, Settings))) return Finish();
			Controller->ApplySelection({});
			if (!Test.TestTrue(TEXT("Repeated W arms without selected castle"), Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun))) return Finish();
			if (!Test.TestEqual(TEXT("Repeated public W cast accepted"), Controller->ConfirmCastleWeapon(Castle->GetNavAgentLocation() + FVector(0, -900, 0)), ELSWeaponCastResult::Ready)) return Finish();
			RepeatShotEnds = World->GetTimeSeconds() + Settings.Range / Settings.ProjectileSpeed;
			AdvanceTo(20);
			return false;
		}
		if (Phase == 20)
		{
			if (World->GetTimeSeconds() <= RepeatShotEnds || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World.Get()))
				return WaitOrFail(TEXT("Repeated W flight and any resulting navigation work finish"), 15);
			CheckWeaponDoesNotBlockHome(TEXT("Repeated W flight preserves complete castle navigation"));
			CheckProductionAndCoreApproach();
			if (!BeginDisarm()) return Finish();
			AdvanceTo(5);
			return false;
		}
		if (Phase == 5)
		{
			if (!ObstacleObservation->bMineDestroyed) return WaitOrFail(TEXT("Actual public mine disarm completes"), 20);
			Test.TestTrue(TEXT("Mine reached Disarmed before its destruction"), ObstacleObservation->bMineDisarmed);
			Test.TestTrue(TEXT("Actual mine work consumed its configured duration"), World->GetTimeSeconds() - DisarmStarted >= DisarmDuration);
			Test.TestTrue(TEXT("Other authored mine remains armed"), OtherMine.IsValid() && OtherMine->IsArmed());
			Test.TestEqual(TEXT("Disarming one mine preserves the other 47 hazards"), World->GetSubsystem<ULSMineHazardSubsystem>()->GetMines().Num(), 47);
			Test.TestTrue(TEXT("Disarm worker survives"), ULSCombatTargetLibrary::IsCombatAlive(Worker.Get()));
			if (!BeginCoreMelee()) return Finish();
			AdvanceTo(17);
			return false;
		}
		if (Phase == 17)
		{
			FLSHealthSnapshot Health;
			Setup->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
			if (Health.CurrentHealth >= CoreHealth)
			{
				if (FPlatformTime::Seconds() - PhaseStarted >= 30)
				{
					AAIController* AI = Cast<AAIController>(Worker->GetController());
					Test.AddInfo(FString::Printf(TEXT("Core melee timeout worker=%s location=%s velocity=%s order=%d failure=%d destination=%s attackTarget=%s engaged=%s distance=%.6f range=%.6f attackActive=%d configured=%d controllerStatus=%d knockedBack=%d"),
						*Worker->GetPathName(), *Worker->GetActorLocation().ToString(), *Worker->GetVelocity().ToString(), static_cast<int32>(Worker->Orders->GetOrderKind()), static_cast<int32>(Worker->Orders->GetLastFailure()),
						*Worker->Orders->GetOrderDestination().ToString(), *GetPathNameSafe(Worker->BasicAttack->GetAttackTarget()), *GetPathNameSafe(Worker->Orders->GetEngagedTarget()),
						ULSCombatTargetLibrary::GetAttackDistance(Worker.Get(), Setup->CoreOutpost), Worker->BasicAttack->GetAttackSettings().Range, Worker->BasicAttack->IsActive(), Worker->BasicAttack->IsConfigured(),
						AI ? static_cast<int32>(AI->GetMoveStatus()) : -1, Worker->Orders->IsKnockedBack()));
				}
				return WaitOrFail(TEXT("Actual staged Core melee approach and damage"), 30);
			}
			Test.TestTrue(TEXT("Public facility attack traverses approach before damage"), FVector::Dist2D(CoreMeleeStart, Worker->GetActorLocation()) > 300);
			Test.TestTrue(TEXT("Actual facility damage occurs within unmodified melee range"), ULSCombatTargetLibrary::GetAttackDistance(Worker.Get(), Setup->CoreOutpost) <= Worker->BasicAttack->GetAttackSettings().Range);
			Controller->HoldSelected();
			if (!BeginBarricadeAttack()) return Finish();
			AdvanceTo(8);
			return false;
		}
		if (Phase == 8)
		{
			const FString BarrierId = BarricadeIds[BarricadeIndex].ToString();
			FLSHealthSnapshot Health;
			if (!Barricade.IsValid() || !Barricade->GetHealthComponent()->TryGetHealth(Health))
			{
				Test.AddError(TEXT("Barricade disappeared before observing public attack damage"));
				return Finish();
			}
			if (Health.CurrentHealth >= BarricadeHealth)
			{
				if (FPlatformTime::Seconds() - PhaseStarted >= 30) ReportObstacleIsolation(BarrierId + TEXT(" damage timeout"));
				return WaitOrFail(*(TEXT("Full-path public worker attack damages ") + BarrierId), 30);
			}
			Test.TestTrue(BarrierId + TEXT(" public worker attack reduced real health"), Health.CurrentHealth < BarricadeHealth);
			if (bOpeningGates) CheckOpenedRegion(FName(*BarrierId.RightChop(7)));
			Controller->HoldSelected();
			Test.TestEqual(BarrierId + TEXT(" synthetic finish destroys damaged barricade"), Barricade->GetHealthComponent()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(Health.MaxHealth * 2, Worker.Get(), Worker.Get())).Status, ELSApplyDamageStatus::Applied);
			Test.TestTrue(BarrierId + TEXT(" destruction was observed"), ObstacleObservation->bBarricadeDestroyed);
			Test.TestTrue(BarrierId + TEXT(" disabled all collision before destruction"), ObstacleObservation->bBarricadeCollisionDisabled);
			AdvanceTo(9);
			return false;
		}
		if (Phase == 9)
		{
			const FString BarrierId = BarricadeIds[BarricadeIndex].ToString();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
			if (FPlatformTime::Seconds() - PhaseStarted < 0.25 || Nav->IsNavigationBuildInProgress()) return WaitOrFail(*(BarrierId + TEXT(" removal navigation rebuild")), 20);
			const ANavigationData* Data = Nav->GetNavDataForProps(Worker->GetNavAgentPropertiesRef(), Worker->GetNavAgentLocation());
			FVector Hit;
			const bool bBlocked = Data->Raycast(BarricadeCrossFrom, BarricadeCrossTo, Hit, Data->GetDefaultQueryFilter(), Worker.Get());
			if (bBlocked)
			{
				if (FPlatformTime::Seconds() - PhaseStarted >= 20)
					Test.AddInfo(FString::Printf(TEXT("%s post-destruction NavData ray remains blocked: from=%s to=%s hit=%s profile=%s"), *BarrierId, *BarricadeCrossFrom.ToString(), *BarricadeCrossTo.ToString(), *Hit.ToString(), *Data->GetConfig().Name.ToString()));
				return WaitOrFail(*(BarrierId + TEXT(" direct navigation recovers after destruction")), 20);
			}
			Test.TestFalse(BarrierId + TEXT(" direct navigation is restored"), bBlocked);
			FNavPathSharedPtr Crossing;
			if (Test.TestTrue(BarrierId + TEXT(" complete 142/284 path after destruction"), CompletePath(Nav, Data, Worker.Get(), BarricadeCrossFrom, BarricadeCrossTo, nullptr, &Crossing)))
				Test.TestTrue(BarrierId + TEXT(" recovered path crosses its local opening"), Crossing.IsValid() && !Crossing->IsPartial());
			if (bOpeningGates)
			{
				const TPair<FVector, FVector>* Opening = CastleGateOpenings.Find(FName(*BarrierId));
				if (!Test.TestNotNull(BarrierId + TEXT(" retained its actual Castle-profile opening endpoints"), Opening)) return Finish();
				const FVector From = BarrierId == TEXT("B_Gate_Core") ? FVector(18500, 1800, 0) : Opening->Key;
				const FVector To = BarrierId == TEXT("B_Gate_Core") ? FVector(20000, 1800, 0) : Opening->Value;
				const ANavigationData* CastleData = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
				FNavPathSharedPtr CastleCrossing;
				FString Failure;
				const bool bComplete = CompletePath(Nav, CastleData, Castle.Get(), From, To, nullptr, &CastleCrossing, &Failure);
				if (!bComplete && FPlatformTime::Seconds() - PhaseStarted < 20) return false;
				if (!Test.TestTrue(BarrierId + TEXT(" restores complete physical500/Nav580 opening path: ") + Failure, bComplete)) return Finish();
				Test.TestEqual(BarrierId + TEXT(" opening query uses Castle radius"), CastleData->GetConfig().AgentRadius, CastleNavigationRadius);
				Test.AddInfo(FString::Printf(TEXT("%s opened physical500/Nav580 path from=%s to=%s points=%d partial=%d"), *BarrierId, *From.ToString(), *To.ToString(), CastleCrossing->GetPathPoints().Num(), CastleCrossing->IsPartial()));
			}
			if (!Test.TestTrue(BarrierId + TEXT(" preserves the other armed mine"), OtherMine.IsValid() && OtherMine->IsArmed())) ReportObstacleIsolation(BarrierId + TEXT(" independent mine changed"));
			++BarricadeIndex;
			if (BarricadeIndex < BarricadeIds.Num())
			{
				if (!BeginBarricadeAttack()) return Finish();
				AdvanceTo(8);
				return false;
			}
			if (bOpeningGates)
			{
				bOpeningGates = false;
				BarricadeIds = {TEXT("B_C1_01"), TEXT("B_R1_01"), TEXT("B_R2_01")};
				BarricadeIndex = 0;
				const ANavigationData* CastleData = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
				CheckRoutes(Nav, Data);
				CheckRoutes(Nav, CastleData);
				if (!BeginGuardEncounter(Nav, CastleData)) return Finish();
				AdvanceTo(10);
				return false;
			}
			if (!BeginRemoteMineChain()) return Finish();
			AdvanceTo(16);
			return false;
		}
		if (Phase == 16)
		{
			if (!ObstacleObservation->bChainAExploded || !ObstacleObservation->bChainBExploded) return WaitOrFail(TEXT("Remote targeted ballista detonates authored six-mine cluster"), 10);
			Test.TestTrue(TEXT("Remote hit and adjacent chain mine both explode rather than disarm"), ObstacleObservation->bChainAExploded && ObstacleObservation->bChainBExploded);
			Test.TestTrue(TEXT("Independent authored mine stays armed after remote chain"), OtherMine.IsValid() && OtherMine->IsArmed());
			Test.TestTrue(TEXT("Remote castle remains alive outside blast"), ULSCombatTargetLibrary::IsCombatAlive(Castle.Get()));
			ApplyTerminalDamage();
			AdvanceTo(6);
			return false;
		}
		if (Phase == 6)
		{
			if (!GameState->HasBattleEnded()) return WaitOrFail(TEXT("Real GameMode finalizes injected terminal damage"), 5);
			Test.TestEqual(TEXT("Observed terminal outcome"), GameState->GetBattleOutcome(), bDefeat ? ELSBattleOutcome::Defeat : ELSBattleOutcome::Victory);
			FLSOrder Move;
			Move.Kind = ELSOrderKind::Move;
			Move.Destination = HomeGround;
			Test.TestFalse(TEXT("New castle order rejected after result"), Castle->Orders->SubmitOrder(Move));
			const int32 Population = GameState->GetCurrentPopulation();
			Controller->ProduceTroop(ELSTroopType::Spearman);
			Test.TestEqual(TEXT("Controller production stopped after result"), Controller->GetLastProductionResult(), ELSProductionResult::Unavailable);
			Test.TestEqual(TEXT("No population change after result"), GameState->GetCurrentPopulation(), Population);
			Test.AddInfo(FString::Printf(TEXT("%s: actual PIE startup/two-body nav/vision%s; %s observed using synthetic terminal damage, not a player victory run."),
				*Preset.ToString(), bDefeat ? TEXT("") : TEXT("/isolated repeated Guard melee/ground W-E hits/persistent HUD queries/selection-independent QWE/four-heading moving production and idle yield/castle roundtrips/Core complete approach and actual melee damage/mine disarm/remote mine chain/seven gates and three supporting panels approached and hit then synthetically destroyed"), bDefeat ? TEXT("Defeat") : TEXT("Victory")));
			return Finish();
		}
		return false;
	}
private:
	bool CheckFrontApproachIsolation(const ANavigationData* TroopData)
	{
		FAutomationTestBase& Test = *State->Test;
		const auto* Coordinator = FindOnlyActor<ALSBattlefieldEncounterCoordinator>(World.Get());
		if (!Test.TestNotNull(TEXT("Front isolation reads actual encounter membership"), Coordinator)) return false;
		const double TroopNavRadius = TroopData->GetConfig().AgentRadius;
		float CastleRadius, CastleHeight;
		Castle->GetSimpleCollisionCylinder(CastleRadius, CastleHeight);
		const FVector Start(0, 0, 0), Junction(2500, 0, 0);
		struct FLSFrontApproach
		{
			FName RegionId;
			TArray<FVector> Points;
		};
		const TArray<FLSFrontApproach> Approaches = {
			{TEXT("L1"), {Start, Junction, FVector(3000, -6300, 0), FVector(4000, -6300, 0), FVector(4200, -6300, 0)}},
			{TEXT("C1"), {Start, Junction, FVector(4000, 0, 0), FVector(4200, 0, 0)}},
			{TEXT("R1"), {Start, Junction, FVector(3000, 4600, 0), FVector(4000, 4600, 0), FVector(4200, 4600, 0)}}
		};
		bool bValid = true;
		for (const FLSEncounterRegion& Region : Coordinator->Regions)
		{
			if (Region.RegionId != TEXT("L1") && Region.RegionId != TEXT("C1") && Region.RegionId != TEXT("R1")) continue;
			for (AActor* Target : Region.AlarmTargets)
			{
				if (const auto* Turret = Cast<ALSTurret>(Target))
				{
					const UPrimitiveComponent* Body = Turret->GetObstacleBody();
					bValid &= Test.TestTrue(Region.RegionId.ToString() + TEXT(" front turret authoritative footprint is east of the mouth"),
						Body && Body->Bounds.GetBox().Min.X >= 4200.0 - .1);
				}
				const auto* Troop = Cast<ALSEnemyTroop>(Target);
				if (!Troop || Troop->Behavior->Role != ELSEnemyRole::Guard) continue;
				const auto* Behavior = Troop->Behavior.Get();
				const FString Label = Region.RegionId.ToString() + TEXT(" ") + Troop->GetActorLabel();
				if (!Test.TestTrue(Label + TEXT(" retains outpost-mode support ownership"), IsValid(Behavior->GuardedOutpost) && !Behavior->GuardAnchor))
				{
					bValid = false;
					continue;
				}
				float TroopRadius, TroopHeight;
				Troop->GetSimpleCollisionCylinder(TroopRadius, TroopHeight);
				const FVector Initial = Behavior->GetInitialPosition();
				bValid &= Test.TestTrue(Label + TEXT(" initial root has full troop-nav and body clearance east of the mouth"),
					Initial.X >= 4200.0 + TroopNavRadius + TroopRadius - .1);
				const FVector Anchor = Behavior->GuardedOutpost->GetActorLocation();
				const double DomainRadius = Behavior->GuardedOutpost->GetDefenseRadius();
				bValid &= Test.TestTrue(Label + TEXT(" initial root is inside its actual outpost defense domain"),
					DomainRadius > 0 && FVector::Dist2D(Initial, Anchor) <= DomainRadius);
				if (!Test.TestTrue(Label + TEXT(" has initialized actual attack settings"), Troop->BasicAttack->IsConfigured()))
				{
					bValid = false;
					continue;
				}
				const double AttackRange = Troop->BasicAttack->GetAttackSettings().Range;
				// 접근 중심선 양옆 1m와 추가 50cm 여유도 보호한다. 벽 차폐나 감지 실패에 기대지 않는다.
				const double Envelope = DomainRadius + Behavior->BoundaryAllowance + AttackRange + TroopRadius + CastleRadius + 100.0 + 50.0;
				const FVector Center(Anchor.X, Anchor.Y, 0);
				double ProtectedDistance = FVector::Dist2D(Center, Junction);
				FString Closest = TEXT("shared J0");
				for (const FLSFrontApproach& Approach : Approaches)
				{
					if (Approach.RegionId == Region.RegionId) continue;
					auto IncludeSegment = [&](const FVector& From, const FVector& To, const TCHAR* Kind)
					{
						const double Distance = FMath::PointDistToSegment(Center, From, To);
						if (Distance < ProtectedDistance)
						{
							ProtectedDistance = Distance;
							Closest = Approach.RegionId.ToString() + TEXT(" ") + Kind;
						}
					};
					for (int32 Index = 1; Index < Approach.Points.Num(); ++Index)
						IncludeSegment(Approach.Points[Index - 1], Approach.Points[Index], TEXT("existing approach"));
					if (Approach.RegionId != TEXT("C1")) IncludeSegment(Start, Approach.Points[2], TEXT("direct staging approach"));
				}
				bValid &= Test.TestTrue(Label + TEXT(" leash and real firing envelope exclude shared J0 and foreign approaches"), ProtectedDistance > Envelope);
				Test.AddInfo(FString::Printf(TEXT("%s domain=%.1f boundary=%.1f attack=%.1f troopRadius=%.1f castleRadius=%.1f protectedHalfWidth=100 safety=50 envelope=%.1f closest=%s distance=%.1f margin=%.1fcm"),
					*Label, DomainRadius, Behavior->BoundaryAllowance, AttackRange, TroopRadius, CastleRadius, Envelope, *Closest, ProtectedDistance, ProtectedDistance - Envelope));
			}
		}
		return bValid;
	}

	void CheckEncounterDormancy()
	{
		FAutomationTestBase& Test = *State->Test;
		Encounter = FindOnlyActor<ALSBattlefieldEncounterCoordinator>(World.Get());
		if (!Test.TestNotNull(TEXT("One authored regional encounter coordinator"), Encounter.Get())) return;
		Test.TestTrue(TEXT("Encounter references and placement bounds validate"), Encounter->GetConfigurationError().IsEmpty());
		Test.TestEqual(TEXT("Eight explicit encounter regions"), Encounter->Regions.Num(), 8);
		int32 Dormant = 0, AnchoredGuards = 0;
		const TMap<FName, int32> ExpectedAssaults = {{TEXT("L1"), 6}, {TEXT("L2"), 6}, {TEXT("C1"), 18}, {TEXT("C2"), 18},
			{TEXT("R1"), 12}, {TEXT("R2"), 12}, {TEXT("Core"), 12}, {TEXT("CentralGuardOnly"), 0}};
		TMap<FName, int32> ActualAssaults;
		for (const FLSEncounterRegion& Region : Encounter->Regions)
		{
			Test.TestFalse(Region.RegionId.ToString() + TEXT(" starts unactivated"), Encounter->IsRegionActivated(Region.RegionId));
			const int32* Expected = ExpectedAssaults.Find(Region.RegionId);
			Test.TestFalse(Region.RegionId.ToString() + TEXT(" has unique authored region ID"), ActualAssaults.Contains(Region.RegionId));
			ActualAssaults.Add(Region.RegionId, Region.AssaultMembers.Num());
			if (Test.TestNotNull(Region.RegionId.ToString() + TEXT(" has explicit asymmetric Assault requirement"), Expected))
				Test.TestEqual(Region.RegionId.ToString() + TEXT(" authored Assault membership"), Region.AssaultMembers.Num(), *Expected);
			const bool bFront = Region.RegionId == TEXT("L1") || Region.RegionId == TEXT("C1") || Region.RegionId == TEXT("R1");
			const bool bRear = Region.RegionId == TEXT("L2") || Region.RegionId == TEXT("C2") || Region.RegionId == TEXT("R2");
			if (bFront || bRear)
			{
				double EntryMinX = TNumericLimits<double>::Max(), AssaultMinX = TNumericLimits<double>::Max();
				for (const FBox& Bounds : Region.EntryBounds) EntryMinX = FMath::Min(EntryMinX, Bounds.Min.X);
				for (const FBox& Bounds : Region.AssaultPlacementBounds) AssaultMinX = FMath::Min(AssaultMinX, Bounds.Min.X);
				if (Test.TestFalse(Region.RegionId.ToString() + TEXT(" has native entry bounds"), Region.EntryBounds.IsEmpty()))
					Test.TestEqual(Region.RegionId.ToString() + TEXT(" native entry begins at intended X"), EntryMinX, bFront ? 4000.0 : 12200.0, 0.1);
				if (Test.TestFalse(Region.RegionId.ToString() + TEXT(" has native Assault placement bounds"), Region.AssaultPlacementBounds.IsEmpty()))
					Test.TestEqual(Region.RegionId.ToString() + TEXT(" native Assault placement begins at intended X"), AssaultMinX, bFront ? 4600.0 : 13400.0, 0.1);
			}
			for (AActor* Member : Region.AssaultMembers)
			{
				if (!Test.TestNotNull(Region.RegionId.ToString() + TEXT(" native Assault member exists"), Member)) continue;
				if (bFront || bRear)
					Test.TestTrue(Region.RegionId.ToString() + TEXT(" actual Assault actor stays behind its intended western placement boundary"), Member->GetActorLocation().X >= (bFront ? 4600.0 : 13400.0) - 0.1);
				auto* Behavior = Member->FindComponentByClass<ULSEnemyBehaviorComponent>();
				if (Test.TestTrue(TEXT("Authored Assault stays regionally dormant"), Behavior && Behavior->IsRegionallyDormant())) ++Dormant;
			}
			for (AActor* Target : Region.AlarmTargets)
				if (auto* Troop = Cast<ALSEnemyTroop>(Target); Troop && Troop->Behavior->Role == ELSEnemyRole::Guard)
				{
					const auto* Behavior = Troop->Behavior.Get();
					const AActor* Anchor = Behavior->GuardAnchor ? Behavior->GuardAnchor.Get() : static_cast<AActor*>(Behavior->GuardedOutpost.Get());
					const float Radius = Behavior->GuardAnchor ? Behavior->GuardAnchorRadius : Behavior->GuardedOutpost ? Behavior->GuardedOutpost->GetDefenseRadius() : 0;
					if (Test.TestTrue(TEXT("Every Guard retains its real regional anchor and defense domain"), Anchor && Radius > 0 && FVector::Dist2D(Troop->GetActorLocation(), Anchor->GetActorLocation()) <= Radius)) ++AnchoredGuards;
				}
		}
		for (const TPair<FName, int32>& Expected : ExpectedAssaults)
			Test.TestTrue(Expected.Key.ToString() + TEXT(" required region exists"), ActualAssaults.Contains(Expected.Key));
		Test.TestEqual(TEXT("Left-lane Assault burden"), ActualAssaults.FindRef(TEXT("L1")) + ActualAssaults.FindRef(TEXT("L2")), 12);
		Test.TestEqual(TEXT("Center-lane Assault burden"), ActualAssaults.FindRef(TEXT("C1")) + ActualAssaults.FindRef(TEXT("C2")), 36);
		Test.TestEqual(TEXT("Right-lane Assault burden"), ActualAssaults.FindRef(TEXT("R1")) + ActualAssaults.FindRef(TEXT("R2")), 24);
		Test.TestEqual(TEXT("All 84 authored Assaults are dormant before local contact"), Dormant, 84);
		Test.TestEqual(TEXT("All 186 authored Guards use actual defense anchors"), AnchoredGuards, 186);
	}
	void CheckOpenedRegion(FName RegionId)
	{
		FAutomationTestBase& Test = *State->Test;
		if (!Test.TestNotNull(TEXT("Actual gate damage retains encounter authority"), Encounter.Get())) return;
		OpenedRegions.Add(RegionId);
		for (const FLSEncounterRegion& Region : Encounter->Regions)
		{
			const bool bExpected = OpenedRegions.Contains(Region.RegionId);
			Test.TestEqual(Region.RegionId.ToString() + TEXT(" activates only after its own gate contact"), Encounter->IsRegionActivated(Region.RegionId), bExpected);
			for (AActor* Member : Region.AssaultMembers)
			{
				auto* Behavior = Member->FindComponentByClass<ULSEnemyBehaviorComponent>();
				Test.TestEqual(TEXT("Regional activation changes every assigned Assault exactly once"), Behavior->IsRegionallyDormant(), !bExpected);
				if (bExpected) Test.TestFalse(TEXT("An activated Assault rejects duplicate activation"), Behavior->ActivateRegion());
			}
		}
	}
	void CheckClosedGates(UNavigationSystemV1* Nav, const ANavigationData* Data)
	{
		FAutomationTestBase& Test = *State->Test;
		for (FName Id : BarricadeIds)
		{
			ALSBarricade* Gate = FindNamedActor<ALSBarricade>(World.Get(), RegistryNames.FindRef(Id));
			FVector From, To;
			FString Details;
			if (!Test.TestTrue(Id.ToString() + TEXT(" has a blocked local body crossing for ") + Data->GetConfig().Name.ToString(),
				Gate && FindBarricadeCrossing(Nav, Data, Gate, HomeGround.Z, From, To, Details))) { Test.AddInfo(Details); continue; }
			Test.TestFalse(Id.ToString() + TEXT(" intact gate has no complete bypass through another lane"), CompletePath(Nav, Data, Castle.Get(), From, To));
			if (FMath::IsNearlyEqual(Data->GetConfig().AgentRadius, CastleNavigationRadius)) CastleGateOpenings.Add(Id, TPair<FVector, FVector>(From, To));
		}
	}
	bool BeginGuardEncounter(UNavigationSystemV1* Nav, const ANavigationData* Data)
	{
		FAutomationTestBase& Test = *State->Test;
		Worker->BasicAttack->EndBattleActions();
		FLSOrder Hold;
		Hold.Kind = ELSOrderKind::Hold;
		Worker->Orders->SubmitOrder(Hold);
		Worker->SetActorLocation(Home + FVector(-1400, -1200, -412), false, nullptr, ETeleportType::TeleportPhysics);
		FLSHealthSnapshot Health;
		Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
		EncounterHealth = Health.CurrentHealth;
		FLSUnitStatRow EnemyStats;
		if (!Test.TestTrue(TEXT("Chosen authored Guard retains initialized real stats"), MeleeEnemy->TryGetStats(EnemyStats))) return false;
		const ULSEnemyBehaviorComponent* Behavior = MeleeEnemy->Behavior;
		const AActor* Anchor = Behavior->GuardAnchor ? Behavior->GuardAnchor.Get() : static_cast<AActor*>(Behavior->GuardedOutpost.Get());
		const float DomainRadius = Behavior->GuardAnchor ? Behavior->GuardAnchorRadius : Behavior->GuardedOutpost ? Behavior->GuardedOutpost->GetDefenseRadius() : 0;
		if (!Test.TestTrue(TEXT("Guard calibration has its actual authored defense domain"), Anchor && DomainRadius > 0)) return false;
		Test.AddInfo(FString::Printf(TEXT("Guard fixture actor=%s position=%s anchor=%s anchorPosition=%s domainRadius=%.1f detection=%.1f meleeRange=%.1f"),
			*MeleeEnemy->GetActorLabel(), *MeleeEnemy->GetActorLocation().ToString(), *Anchor->GetActorLabel(), *Anchor->GetActorLocation().ToString(), DomainRadius, EnemyStats.DetectionRange, MeleeEnemy->BasicAttack->GetAttackSettings().Range));
		UCapsuleComponent* Capsule = Castle->GetCapsuleComponent();
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSGuardCastleFixtureStation), false, Castle.Get());
		const TArray<FLSMineSphere> Mines = LSMineAvoidance::Snapshot(World.Get());
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const double Angle = -PI * 0.5 + 2 * PI * Index / 16;
			const FVector Candidate = MeleeEnemy->GetNavAgentLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 880;
			FNavLocation Contact;
			if (!Nav->ProjectPointToNavigation(Candidate, Contact, FVector(200, 200, 500), Data) || FVector::Dist2D(Candidate, Contact.Location) > 200)
			{
				Test.AddInfo(FString::Printf(TEXT("Guard fixture rejects radial station %d candidate=%s: Castle580 projection exceeds200cm or fails"), Index, *Candidate.ToString()));
				continue;
			}
			const FVector Station = Contact.Location + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
			const double MeleeDistance = ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Castle.Get(), MeleeEnemy.Get(), Station);
			if (MeleeDistance <= MeleeEnemy->BasicAttack->GetAttackSettings().Range || FVector::Distance(Station, MeleeEnemy->GetActorLocation()) > EnemyStats.DetectionRange ||
				FVector::Dist2D(Contact.Location, Anchor->GetActorLocation()) > DomainRadius)
			{
				Test.AddInfo(FString::Printf(TEXT("Guard fixture rejects radial station %d at=%s: boundaryDistance=%.1f detectionDistance=%.1f domainDistance=%.1f"), Index,
					*Station.ToString(), MeleeDistance, FVector::Distance(Station, MeleeEnemy->GetActorLocation()), FVector::Dist2D(Contact.Location, Anchor->GetActorLocation())));
				continue;
			}
			if (World->OverlapBlockingTestByChannel(Station, Castle->GetActorQuat(), Capsule->GetCollisionObjectType(),
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
				FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())) || !LSMineAvoidance::IsSegmentSafe(Castle.Get(), Contact.Location, Contact.Location, Mines))
			{
				Test.AddInfo(FString::Printf(TEXT("Guard fixture rejects radial station %d at=%s: actual castle capsule overlap or live mine contact"), Index, *Station.ToString()));
				continue;
			}
			Castle->SetActorLocation(Station, false, nullptr, ETeleportType::TeleportPhysics);
			FVector AttackGoal;
			if (!FLSEnemySupportTestAccess::FindAttackGoal(Behavior, Castle.Get(), AttackGoal) || FVector::Dist2D(AttackGoal, Anchor->GetActorLocation()) > DomainRadius)
			{
				Test.AddInfo(FString::Printf(TEXT("Guard fixture rejects radial station %d: native Guard cannot resolve an attack goal inside its actual domain"), Index));
				continue;
			}
			Test.AddInfo(FString::Printf(TEXT("Guard fixture chooses radial station %d castle=%s nativeAttackGoal=%s boundaryDistance=%.1f; natural Guard AI must approach and damage without an injected target/order."),
				Index, *Station.ToString(), *AttackGoal.ToString(), MeleeDistance));
			MeleeEnemy->Behavior->SetComponentTickEnabled(true);
			return true;
		}
		Castle->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
		Test.AddError(TEXT("No valid Castle580 navigation station among16 radius880cm candidates outside melee and inside Guard detection/domain with collision/mine-safe placement."));
		return false;
	}

	void CheckInitialConditions()
	{
		FAutomationTestBase& Test = *State->Test;
		const ELSProductionResult InitialProduction = Controller->Production->GetStatus(ELSTroopType::Spearman);
		Test.TestTrue(TEXT("Deferred initial population collection has completed"), InitialProduction == ELSProductionResult::Ready || InitialProduction == ELSProductionResult::SelectCastle);
		Test.TestEqual(TEXT("Initial 18 troops count toward population"), GameState->GetCurrentPopulation(), 18);
		int32 LivingAllyCastles = 0;
		int32 PublicCoreFlags = 0;
		for (TActorIterator<AActor> It(World.Get()); It; ++It)
		{
			const ULSSelectableComponent* Selection = It->FindComponentByClass<ULSSelectableComponent>();
			const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(*It);
			if (Selection && Selection->Kind == ELSSelectionKind::Castle && Faction && Faction->GetFaction() == ELSFaction::Ally && ULSCombatTargetLibrary::IsCombatAlive(*It)) ++LivingAllyCastles;
			if (const ULSVisionParticipantComponent* Vision = It->FindComponentByClass<ULSVisionParticipantComponent>())
				if (Vision->bPublicCorePosition) ++PublicCoreFlags;
		}
		Test.TestEqual(TEXT("Exactly one living allied castle"), LivingAllyCastles, 1);
		Test.TestEqual(TEXT("Exactly one public Core flag"), PublicCoreFlags, 1);
		Test.TestTrue(TEXT("Configured Core owns public flag"), Setup->CoreOutpost->Vision->bPublicCorePosition);
		Test.TestTrue(TEXT("Core remains armed"), Setup->CoreOutpost->IsArmed());
		for (const FName Id : {FName(TEXT("G_L1")), FName(TEXT("G_C1")), FName(TEXT("G_R1")), FName(TEXT("G_L2")), FName(TEXT("G_C2")), FName(TEXT("G_R2"))})
		{
			ALSOutpost* Outpost = FindNamedActor<ALSOutpost>(World.Get(), RegistryNames.FindRef(Id));
			if (!Test.TestNotNull(*Id.ToString(), Outpost)) continue;
			Test.TestTrue(Id.ToString() + TEXT(" has positive defense radius"), Outpost->GetDefenseRadius() > 0);
			Test.TestFalse(Id.ToString() + TEXT(" remains unarmed"), Outpost->IsArmed());
		}
		for (const ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
		{
			FLSCastleWeaponState WeaponState;
			if (!Test.TestTrue(TEXT("Castle Q/W/E weapon state available"), Castle->Weapons->GetWeaponState(Weapon, WeaponState))) continue;
			Test.TestTrue(TEXT("Castle weapon unlocked at start"), WeaponState.bUnlocked);
			Test.TestTrue(TEXT("Castle weapon enabled at start"), WeaponState.bEnabled);
			Test.TestEqual(TEXT("Castle weapon starts at level one"), WeaponState.Level, 1);
		}
		int32 AlliedTypes[3] = {}, EnemyTypes[3] = {};
		int32 Turrets = 0, Barricades = 0, Mines = 0, Anchors = 0;
		for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
		{
			if (It->GetFaction() == ELSFaction::Ally) ++AlliedTypes[static_cast<int32>(It->GetTroopType())];
			else if (It->GetFaction() == ELSFaction::Enemy) ++EnemyTypes[static_cast<int32>(It->GetTroopType())];
		}
		Test.TestEqual(TEXT("Initial allied spearmen"), AlliedTypes[0], 8);
		Test.TestEqual(TEXT("Initial allied archers"), AlliedTypes[1], 6);
		Test.TestEqual(TEXT("Initial allied cavalry"), AlliedTypes[2], 4);
		Test.TestEqual(TEXT("Authored enemy spearmen"), EnemyTypes[0], 126);
		Test.TestEqual(TEXT("Authored enemy archers"), EnemyTypes[1], 110);
		Test.TestEqual(TEXT("Authored enemy cavalry"), EnemyTypes[2], 34);
		for (TActorIterator<ALSTurret> It(World.Get()); It; ++It) ++Turrets;
		for (TActorIterator<ALSBarricade> It(World.Get()); It; ++It) ++Barricades;
		for (TActorIterator<ALSMine> It(World.Get()); It; ++It) ++Mines;
		for (TActorIterator<ALSOutpost> It(World.Get()); It; ++It) if (*It != Setup->CoreOutpost) ++Anchors;
		Test.TestEqual(TEXT("Turret density excludes armed Core"), Turrets, 18);
		Test.TestEqual(TEXT("Barricade density"), Barricades, 25);
		Test.TestEqual(TEXT("Mine density"), Mines, 48);
		Test.TestEqual(TEXT("Six non-core anchors"), Anchors, 6);
		Test.TestEqual(TEXT("Castle collision radius"), Castle->GetCapsuleComponent()->GetScaledCapsuleRadius(), 500.0f);
		Test.TestEqual(TEXT("Castle collision half-height"), Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 500.0f);
		Test.TestTrue(TEXT("Castle actor scale stays one"), Castle->GetActorScale3D().Equals(FVector::OneVector));
		FBox VisualBounds(ForceInit);
		TInlineComponentArray<UStaticMeshComponent*> Parts(Castle.Get());
		for (UStaticMeshComponent* Part : Parts)
			if (Part->GetFName() == TEXT("Body") || Part->GetFName() == TEXT("LeftTower") || Part->GetFName() == TEXT("RightTower"))
				VisualBounds += Part->Bounds.GetBox();
		Test.TestEqual(TEXT("Castle sphere length 10m"), VisualBounds.GetSize().X, 1000.0, 1.0);
		Test.TestEqual(TEXT("Castle visual width 10m"), VisualBounds.GetSize().Y, 1000.0, 1.0);
		const ULSCastleMovementComponent* Movement = Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent());
		if (Test.TestNotNull(TEXT("Castle uses path-owned turn-before-forward motor"), Movement))
		{
			Test.TestEqual(TEXT("Physical500 castle uses logical580 navigation radius"), Movement->NavAgentProps.AgentRadius, CastleNavigationRadius);
			Test.TestEqual(TEXT("Castle logical navigation height stays1000"), Movement->NavAgentProps.AgentHeight, 1000.0f);
			Test.TestTrue(TEXT("Battlefield castle opts into automatic turning"), Movement->bTurnBeforeForward);
			Test.TestEqual(TEXT("Automatic turn rate is 90 degrees/sec"), Movement->TurnRate, 90.0f);
			Test.TestTrue(TEXT("Automatic forward acceleration/braking configured"), Movement->ForwardAcceleration > 0 && Movement->ForwardBrakingDeceleration > 0);
		}
		const FStructProperty* StatsProperty = FindFProperty<FStructProperty>(ALSCastle::StaticClass(), TEXT("StatsRow"));
		const auto* StatsHandle = StatsProperty->ContainerPtrToValuePtr<FDataTableRowHandle>(Castle.Get());
		Test.TestEqual(TEXT("Actual castle retains original Castle stats table"), GetPathNameSafe(StatsHandle->DataTable), FString(TEXT("/Game/Landship/Combat/Data/DT_CastleStats.DT_CastleStats")));
		Test.TestEqual(TEXT("Actual castle selects its battlefield Castle row"), StatsHandle->RowName, FName(TEXT("BattlefieldPrototype")));
		FLSCastleStatRow Stats;
		if (Test.TestTrue(TEXT("Castle real stats initialized"), Castle->TryGetStats(Stats)))
		{
			Test.TestEqual(TEXT("Castle health from original typed row"), Stats.Health, 1000.0f);
			Test.TestEqual(TEXT("Castle sight from original typed row"), Stats.VisionRadius, 1600.0f);
			Test.TestEqual(TEXT("Castle speed 4m/s"), Stats.MovementSpeed, 400.0f);
		}
		if (Castle->TryGetStats(Stats)) Test.AddInfo(FString::Printf(TEXT("%s actual castle movement speed: %.1f cm/s"), *Preset.ToString(), Stats.MovementSpeed));
	}
	void CheckWeaponDoesNotBlockHome(const TCHAR* Label)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		const ANavigationData* Data = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
		FString Failure;
		const bool bComplete = CompletePath(Nav, Data, Castle.Get(), HomeGround, HomeGround + FVector(1800, 0, 0), nullptr, nullptr, &Failure);
		State->Test->TestTrue(FString(Label) + TEXT(": ") + Failure, bComplete);
	}
	bool BeginGroundWeaponHit(ELSCastleWeapon Weapon, FName TargetId)
	{
		FAutomationTestBase& Test = *State->Test;
		WeaponTarget = FindNamedActor<ALSEnemyTroop>(World.Get(), RegistryNames.FindRef(TargetId));
		if (!Test.TestNotNull(TEXT("Ground-hit uses real authored enemy troop"), WeaponTarget.Get())) return false;
		WeaponTargetOriginal = WeaponTarget->GetActorLocation();
		const float Height = WeaponTarget->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Ground = Castle->GetNavAgentLocation() + FVector(0, -900, 0);
		WeaponTarget->GetCharacterMovement()->StopMovementImmediately();
		WeaponTarget->SetActorLocation(Ground + FVector(0, 0, Height), false, nullptr, ETeleportType::TeleportPhysics);
		FLSHealthSnapshot Health;
		if (!WeaponTarget->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health)) return false;
		WeaponTargetHealth = Health.CurrentHealth;
		ALSTroopUnit* Selected = nullptr;
		for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
			if (It->GetFaction() == ELSFaction::Ally) { Selected = *It; break; }
		if (!Test.TestNotNull(TEXT("Allied troop for selection-independent weapon input"), Selected)) return false;
		Controller->ApplySelection({Selected});
		const ELSOrderKind Before = Selected->Orders->GetOrderKind();
		Test.TestTrue(TEXT("Troop-only selection has configured weapon source"), Controller->GetSelectedArmedCastle() == nullptr && Controller->GetCastleWeaponSource() == Castle.Get());
		if (!Test.TestTrue(TEXT("QWE arms without switching troop selection"), Controller->ArmCastleWeapon(Weapon))) return false;
		Test.TestTrue(TEXT("Aim owner is configured castle"), Controller->GetAimingCastle() == Castle.Get());
		const bool bCast = Test.TestEqual(TEXT("Public ground cast accepted"), Controller->ConfirmCastleWeapon(Ground), ELSWeaponCastResult::Ready);
		Test.TestTrue(TEXT("Casting preserves exact troop selection"), Controller->GetSelectedActors() == TArray<AActor*>{Selected});
		Test.TestEqual(TEXT("Casting preserves troop order"), Selected->Orders->GetOrderKind(), Before);
		return bCast;
	}
	void CheckSelectionIndependentWeapons()
	{
		FAutomationTestBase& Test = *State->Test;
		ULSBattleHUDWidget* HUD = CreateWidget<ULSBattleHUDWidget>(Controller.Get(), ULSBattleHUDWidget::StaticClass());
		if (!Test.TestNotNull(TEXT("HUD query widget uses real PIE controller"), HUD)) return;
		FLSHealthSnapshot Health;
		Castle->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
		Test.TestEqual(TEXT("Persistent HP query reflects actual enemy melee damage with troop selection"), HUD->GetCastleHealthFraction(), Health.CurrentHealth / Health.MaxHealth, 0.001f);
		Test.TestFalse(TEXT("Persistent HP text available with troop selection"), HUD->GetCastleHealthText().IsEmpty());
		const FLSCastleWeaponHUDState Status = HUD->GetCastleWeaponStatus(ELSCastleWeapon::Ballista);
		Test.TestTrue(TEXT("Persistent HUD observes just-cast weapon cooldown"), Status.Remaining > 0 && Status.Duration > 0 && !Status.bAvailable);
		Controller->ApplySelection({});
		Test.TestEqual(TEXT("Persistent HP survives no selection"), HUD->GetCastleHealthFraction(), Health.CurrentHealth / Health.MaxHealth, 0.001f);
		Test.TestTrue(TEXT("No-selection Q aiming uses configured castle"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
		Test.TestEqual(TEXT("No-selection public Q cast"), Controller->ConfirmCastleWeapon(Castle->GetNavAgentLocation() + FVector(-1000, 0, 0)), ELSWeaponCastResult::Ready);
		Test.TestTrue(TEXT("No-selection Q leaves selection empty"), Controller->GetSelectedActors().IsEmpty());
	}
	void PrepareIdleYield(const FVector& Destination)
	{
		bSawYield = false;
		MinimumForwardDot = 1;
		for (TActorIterator<ALSTroopUnit> It(World.Get()); It; ++It)
			if (It->GetFaction() == ELSFaction::Ally) { YieldTroop = *It; break; }
		if (!State->Test->TestNotNull(TEXT("Initial allied troop available for live yield observation"), YieldTroop.Get())) return;
		FLSOrder Idle;
		YieldTroop->Orders->SubmitOrder(Idle);
		const FVector Direction = (Destination - HomeGround).GetSafeNormal2D();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		const ANavigationData* Data = Nav->GetNavDataForProps(YieldTroop->GetNavAgentPropertiesRef(), YieldTroop->GetNavAgentLocation());
		FNavLocation Staging;
		if (!State->Test->TestTrue(TEXT("42/65cm actual troop matches troop navigation profile"), Data && FMath::IsNearlyEqual(Data->GetConfig().AgentRadius, 142.0f))) return;
		if (!State->Test->TestTrue(TEXT("Idle ally stages ahead on real troop navigation"), Nav->ProjectPointToNavigation(HomeGround + Direction * 1000, Staging, FVector(100, 100, 300), Data))) return;
		YieldStart = Staging.Location + FVector(0, 0, YieldTroop->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		YieldTroop->GetCharacterMovement()->StopMovementImmediately();
		YieldTroop->SetActorLocation(YieldStart, false, nullptr, ETeleportType::TeleportPhysics);
	}
	void CheckProducedCorridor()
	{
		FAutomationTestBase& Test = *State->Test;
		Test.TestEqual(TEXT("Moving production creates a pair"), Controller->Production->GetLastProducedBatch().Num(), 2);
		for (const auto& WeakProduced : Controller->Production->GetLastProducedBatch())
		{
		ALSTroopUnit* Produced = WeakProduced.Get();
		if (!Test.TestNotNull(TEXT("Moving production creates real troop"), Produced)) return;
		TArray<FVector> Path;
		if (!Test.TestTrue(TEXT("Active castle exposes planned path while turning"), Castle->Orders->GetPlannedMovementPath(Path, 2200) && Path.Num() >= 2)) return;
		double Clearance = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Path.Num(); ++Index)
			Clearance = FMath::Min(Clearance, FMath::Sqrt(static_cast<double>(FMath::PointDistToSegmentSquared(Produced->GetNavAgentLocation(), Path[Index - 1], Path[Index]))));
		Test.TestTrue(TEXT("Produced troop clears planned castle movement corridor"), Clearance >= Castle->GetCapsuleComponent()->GetScaledCapsuleRadius() + Produced->GetCapsuleComponent()->GetScaledCapsuleRadius() + 50);
		Test.TestTrue(TEXT("Produced position avoids live mines"), LSMineAvoidance::IsSegmentSafe(Produced, Produced->GetNavAgentLocation(), Produced->GetNavAgentLocation(), LSMineAvoidance::Snapshot(World.Get())));
		Test.TestEqual(TEXT("Production assigns no follow/rally order"), Produced->Orders->GetOrderKind(), ELSOrderKind::Idle);
		}
	}
	void Advance() { AdvanceTo(Phase + 1); }
	void AdvanceTo(int32 NewPhase) { Phase = NewPhase; PhaseStarted = FPlatformTime::Seconds(); }
	bool WaitOrFail(const TCHAR* Description, double Timeout)
	{
		if (FPlatformTime::Seconds() - PhaseStarted < Timeout) return false;
		State->Test->AddError(FString::Printf(TEXT("%s %s timed out after %.0fs"), *Preset.ToString(), Description, Timeout));
		if (Phase == 7) { State->bReady = false; return true; }
		return Finish();
	}
	bool Finish()
	{
		RemoveObstacleObserver();
		GEditor->RequestEndPlayMap();
		AdvanceTo(7);
		return false;
	}
	void RemoveObstacleObserver()
	{
		if (World.IsValid() && ObstacleDestroyedHandle.IsValid()) World->RemoveOnActorDestroyedHandler(ObstacleDestroyedHandle);
		ObstacleDestroyedHandle.Reset();
	}
	bool TeleportWorkerNear(const FVector& Center, float Distance = 600, int32 FirstAngle = 0, int32 CandidateCount = 16)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		const ANavigationData* Data = Nav->GetNavDataForProps(Worker->GetNavAgentPropertiesRef(), Worker->GetNavAgentLocation());
		UCapsuleComponent* Capsule = Worker->GetCapsuleComponent();
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		const double NavHeight = Worker->GetActorLocation().Z - Worker->GetNavAgentLocation().Z;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBattlefieldWorkerPlacement), false, Worker.Get());
		const TArray<FLSMineSphere> Mines = LSMineAvoidance::Snapshot(World.Get());
		for (int32 Index = FirstAngle; Index < FirstAngle + CandidateCount; ++Index)
		{
			const double Angle = 2 * PI * Index / 16;
			FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance;
			Candidate.Z = Worker->GetNavAgentLocation().Z;
			FNavLocation Point;
			if (!Nav->ProjectPointToNavigation(Candidate, Point, FVector(50, 50, 300), Data) ||
				!LSMineAvoidance::IsSegmentSafe(Worker.Get(), Point.Location, Point.Location, Mines)) continue;
			const FVector ActorPosition = Point.Location + FVector(0, 0, NavHeight);
			if (World->OverlapBlockingTestByChannel(ActorPosition, Worker->GetActorQuat(), Capsule->GetCollisionObjectType(),
				FCollisionShape::MakeCapsule(Radius, HalfHeight), Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) continue;
			FLSOrder Hold;
			Hold.Kind = ELSOrderKind::Hold;
			Worker->Orders->SubmitOrder(Hold);
			Worker->GetCharacterMovement()->StopMovementImmediately();
			// 장거리 공략 대신 작업 지점의 충돌·지뢰 안전성과 이후의 실제 명령만 검사한다.
			if (Worker->SetActorLocation(ActorPosition, false, nullptr, ETeleportType::TeleportPhysics)) return true;
		}
		return false;
	}
	void InstallObstacleObserver()
	{
		const TSharedRef<FLSBattlefieldObstacleObservation> Observation = ObstacleObservation;
		ObstacleDestroyedHandle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda(
			[Observation](AActor* Actor)
			{
				if (Actor->GetFName() == Observation->MineName)
				{
					Observation->bMineDestroyed = true;
					Observation->bMineDisarmed = CastChecked<ALSMine>(Actor)->GetMineState() == ELSMineState::Disarmed;
				}
				if (Actor->GetFName() == Observation->ChainAName)
					Observation->bChainAExploded = CastChecked<ALSMine>(Actor)->GetMineState() == ELSMineState::Spent;
				if (Actor->GetFName() == Observation->ChainBName)
					Observation->bChainBExploded = CastChecked<ALSMine>(Actor)->GetMineState() == ELSMineState::Spent;
				if (Actor->GetFName() == Observation->BarricadeName)
				{
					Observation->bBarricadeDestroyed = true;
					TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
					Observation->bBarricadeCollisionDisabled = !Components.IsEmpty();
					for (const UPrimitiveComponent* Component : Components)
						Observation->bBarricadeCollisionDisabled &= Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
				}
			}));
	}
	bool BeginDisarm()
	{
		FAutomationTestBase& Test = *State->Test;
		if (!Test.TestTrue(TEXT("Produced worker remains available for actual obstacle actions"), Worker.IsValid() && ULSCombatTargetLibrary::IsCombatAlive(Worker.Get()))) return false;
		Mine = FindNamedActor<ALSMine>(World.Get(), RegistryNames.FindRef(TEXT("M_R1_01")));
		OtherMine = FindNamedActor<ALSMine>(World.Get(), RegistryNames.FindRef(TEXT("M_R1_02")));
		if (!Test.TestTrue(TEXT("Real authored mines resolve in PIE"), Mine.IsValid() && OtherMine.IsValid())) return false;
		ObstacleObservation->MineName = Mine->GetFName();
		DisarmDuration = Mine->GetDisarmDuration();
		Test.TestEqual(TEXT("Authored mine disarm duration"), DisarmDuration, 6.0f);
		if (!Test.TestTrue(TEXT("Mine work starts from nearby safe navigation"), TeleportWorkerNear(Mine->GetTriggerCenter()))) return false;
		World->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
		if (!Test.TestTrue(TEXT("Public selection helper selects mine worker"), ULSEnemyVisionTestLibrary::TrySelect(Controller.Get(), Worker.Get()))) return false;
		DisarmStarted = World->GetTimeSeconds();
		const bool bAccepted = Controller->IssueSelectionOrder(ELSOrderKind::Disarm, Mine->GetTriggerCenter(), Mine.Get(), false);
		Test.TestTrue(TEXT("Public controller disarm order accepted"), bAccepted);
		Test.TestTrue(TEXT("Mine has claimed actual worker"), Mine->HasDisarmWorker());
		return bAccepted;
	}
	bool BeginCoreMelee()
	{
		FAutomationTestBase& Test = *State->Test;
		for (int32 Angle = 0; Angle < 16; ++Angle)
		{
			if (!TeleportWorkerNear(Setup->CoreOutpost->GetActorLocation(), 1600, Angle, 1)) continue;
			if (ULSCombatTargetLibrary::GetAttackDistance(Worker.Get(), Setup->CoreOutpost) <= Worker->BasicAttack->GetAttackSettings().Range + 300) continue;
			World->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
			Controller->ApplySelection({Worker.Get()});
			if (!Controller->IssueSelectionOrder(ELSOrderKind::AttackTarget, Setup->CoreOutpost->GetActorLocation(), Setup->CoreOutpost, false)) continue;
			AAIController* AI = Cast<AAIController>(Worker->GetController());
			const FNavPathSharedPtr Path = AI && AI->GetPathFollowingComponent() ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
			if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial() || Path->GetPathPoints().Num() < 2) continue;
			const UCapsuleComponent* Capsule = Worker->GetCapsuleComponent();
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSCoreMeleeFixtureApproach), false, Worker.Get());
			Query.AddIgnoredActor(Setup->CoreOutpost);
			const FVector Offset(0, 0, Worker->GetActorLocation().Z - Worker->GetNavAgentLocation().Z);
			FVector From = Worker->GetActorLocation();
			bool bBlocked = false;
			for (const FNavPathPoint& Point : Path->GetPathPoints())
			{
				const FVector To = Point.Location + Offset;
				FHitResult Hit;
				if (World->SweepSingleByChannel(Hit, From, To, Worker->GetActorQuat(), Capsule->GetCollisionObjectType(),
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
					FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
				{
					Test.AddInfo(FString::Printf(TEXT("Core fixture staging angle %d has an actual body blocking its accepted NavData path: actor=%s component=%s hit=%s actorLocation=%s"), Angle, *GetPathNameSafe(Hit.GetActor()), *GetPathNameSafe(Hit.GetComponent()), *Hit.Location.ToString(), Hit.GetActor() ? *Hit.GetActor()->GetActorLocation().ToString() : TEXT("none")));
					bBlocked = true;
					break;
				}
				From = To;
			}
			if (bBlocked) { Controller->HoldSelected(); continue; }
			CoreMeleeStart = Worker->GetActorLocation();
			FLSHealthSnapshot Health;
			if (!Test.TestTrue(TEXT("Core health before actual worker attack"), Setup->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health))) return false;
			CoreHealth = Health.CurrentHealth;
			Test.AddInfo(FString::Printf(TEXT("Core melee uses freshly produced worker %s and physically clear complete approach at angle %d, start=%s endpoint=%s"), *Worker->GetPathName(), Angle, *CoreMeleeStart.ToString(), *Path->GetPathPoints().Last().Location.ToString()));
			return true;
		}
		Test.AddError(TEXT("No physically clear complete Core melee approach among 16 staged positions; frozen defenders retain collision."));
		return false;
	}

	bool BeginRemoteMineChain()
	{
		FAutomationTestBase& Test = *State->Test;
		ALSMine* First = FindNamedActor<ALSMine>(World.Get(), RegistryNames.FindRef(TEXT("M_R2_01")));
		ALSMine* Second = FindNamedActor<ALSMine>(World.Get(), RegistryNames.FindRef(TEXT("M_R2_02")));
		if (!Test.TestTrue(TEXT("Two members of authored chain cluster is still armed"), First && Second && First->IsArmed() && Second->IsArmed())) return false;
		ObstacleObservation->ChainAName = First->GetFName();
		ObstacleObservation->ChainBName = Second->GetFName();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		const ANavigationData* Data = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
		FNavLocation Staging;
		if (!Test.TestTrue(TEXT("Remote shot staging projects to castle profile"), Nav->ProjectPointToNavigation(First->GetTriggerCenter() + FVector(-1800, 0, 0), Staging, FVector(150, 150, 600), Data))) return false;
		if (!Test.TestTrue(TEXT("Remote castle staging is safe for its actual capsule"), LSMineAvoidance::IsSegmentSafe(Castle.Get(), Staging.Location, Staging.Location, LSMineAvoidance::Snapshot(World.Get())))) return false;
		Castle->GetMovementComponent()->StopMovementImmediately();
		Castle->SetActorLocation(Staging.Location + FVector(0, 0, Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
		Controller->ApplySelection({});
		FVector ObstacleAim;
		Test.TestTrue(TEXT("Obstacle target resolves body-centre aim separately from ground shot"), Castle->Weapons->GetObstacleAimPoint(First, ObstacleAim));
		return Test.TestEqual(TEXT("Actual targeted ballista begins remote mine chain"), Castle->Weapons->CastWeaponAtObstacle(ELSCastleWeapon::Ballista, First), ELSWeaponCastResult::Ready);
	}
	bool BeginBarricadeAttack()
	{
		FAutomationTestBase& Test = *State->Test;
		const FName Id = BarricadeIds[BarricadeIndex];
		const FString Label = Preset.ToString() + TEXT(" ") + Id.ToString();
		Barricade = FindNamedActor<ALSBarricade>(World.Get(), RegistryNames.FindRef(Id));
		if (!Test.TestTrue(Label + TEXT(" remains active before public attack"), Barricade.IsValid() && Barricade->IsObstacleActive())) return false;
		ObstacleObservation->BarricadeName = Barricade->GetFName();
		ObstacleObservation->bBarricadeDestroyed = false;
		ObstacleObservation->bBarricadeCollisionDisabled = false;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
		const ANavigationData* Data = Nav->GetNavDataForProps(Worker->GetNavAgentPropertiesRef(), Worker->GetNavAgentLocation());
		const FVector Center = Barricade->GetActorLocation();
		FString CrossingDetails;
		if (!Test.TestTrue(Label + TEXT(" projected crossing intersects actual body and blocked navigation"), FindBarricadeCrossing(Nav, Data, Barricade.Get(), Worker->GetNavAgentLocation().Z, BarricadeCrossFrom, BarricadeCrossTo, CrossingDetails)))
		{
			Test.AddInfo(Label + TEXT(" crossing failure: ") + CrossingDetails);
			return false;
		}
		FVector Hit;
		Test.TestTrue(Label + TEXT(" blocks direct navigation across its body"), Data->Raycast(BarricadeCrossFrom, BarricadeCrossTo, Hit, Data->GetDefaultQueryFilter(), Worker.Get()));
		if (!bOpeningGates)
		{
			FNavPathSharedPtr Detour;
			if (!Test.TestTrue(Label + TEXT(" has complete detour before destruction"), CompletePath(Nav, Data, Worker.Get(), BarricadeCrossFrom, BarricadeCrossTo, nullptr, &Detour))) return false;
			if (!CheckLocalBarricadeDetour(Test, Label + TEXT(" detour before destruction"), Detour, Barricade.Get(), Data, BattlefieldCenterline(BarricadeIndex == 0 ? 1 : 2), BarricadeCrossFrom, BarricadeCrossTo)) return false;
		}
		Test.TestTrue(Label + TEXT(" has movement collision"), Barricade->GetObstacleBody()->IsCollisionEnabled());
		FLSHealthSnapshot Health;
		if (!Test.TestTrue(Label + TEXT(" real health available"), Barricade->GetHealthComponent()->TryGetHealth(Health))) return false;
		BarricadeHealth = Health.CurrentHealth;
		for (int32 Angle = 0; Angle < 16; ++Angle)
		{
			if (!TeleportWorkerNear(Center, 1400, Angle, 1))
			{
				Test.AddInfo(FString::Printf(TEXT("%s rejects staging angle %d: no projected empty mine-safe 1400cm position"), *Label, Angle));
				continue;
			}
			World->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
			if (!Test.TestTrue(TEXT("Public selection helper selects barricade attacker"), ULSEnemyVisionTestLibrary::TrySelect(Controller.Get(), Worker.Get()))) return false;
			if (!Controller->IssueSelectionOrder(ELSOrderKind::AttackTarget, Center, Barricade.Get(), false))
			{
				Test.AddInfo(FString::Printf(TEXT("%s rejects staging angle %d: public attack failure=%d"), *Label, Angle, static_cast<int32>(Worker->Orders->GetLastFailure())));
				Controller->HoldSelected();
				continue;
			}
			AAIController* AI = Cast<AAIController>(Worker->GetController());
			const FNavPathSharedPtr Path = AI && AI->GetPathFollowingComponent() ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
			if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial() || Path->GetPathPoints().Num() < 2)
			{
				Test.AddInfo(FString::Printf(TEXT("%s rejects staging angle %d: accepted attack lacks a complete executing approach"), *Label, Angle));
				Controller->HoldSelected();
				continue;
			}
			const UCapsuleComponent* Capsule = Worker->GetCapsuleComponent();
			FCollisionQueryParams Query(SCENE_QUERY_STAT(LSBarricadeMeleeFixtureApproach), false, Worker.Get());
			Query.AddIgnoredActor(Barricade.Get());
			const FVector Offset(0, 0, Worker->GetActorLocation().Z - Worker->GetNavAgentLocation().Z);
			FVector From = Worker->GetActorLocation();
			bool bBlocked = false;
			for (const FNavPathPoint& Point : Path->GetPathPoints())
			{
				FHitResult ApproachHit;
				const FVector To = Point.Location + Offset;
				if (World->SweepSingleByChannel(ApproachHit, From, To, Worker->GetActorQuat(), Capsule->GetCollisionObjectType(),
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
					FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
				{
					Test.AddInfo(FString::Printf(TEXT("%s rejects staging angle %d: accepted full approach blocked by actor=%s component=%s hit=%s"), *Label, Angle,
						*GetPathNameSafe(ApproachHit.GetActor()), *GetPathNameSafe(ApproachHit.GetComponent()), *ApproachHit.Location.ToString()));
					bBlocked = true;
					break;
				}
				From = To;
			}
			if (bBlocked) { Controller->HoldSelected(); continue; }
			Test.AddInfo(FString::Printf(TEXT("%s chooses staging angle %d: public accepted complete approach is physically clear; start=%s endpoint=%s"),
				*Label, Angle, *Worker->GetActorLocation().ToString(), *Path->GetPathPoints().Last().Location.ToString()));
			return true;
		}
		ReportObstacleIsolation(Label + TEXT(" no clear staged approach"));
		Test.AddError(Label + TEXT(" has no physically clear complete public attack approach among 16 mine-safe staging angles"));
		return false;
	}

	void ReportObstacleIsolation(const FString& Label)
	{
		FAutomationTestBase& Test = *State->Test;
		if (Worker.IsValid())
		{
			FLSHealthSnapshot Health;
			Worker->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health);
			AAIController* AI = Cast<AAIController>(Worker->GetController());
			const FNavPathSharedPtr Path = AI && AI->GetPathFollowingComponent() ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
			const ULSMineAwareMovementComponent* Movement = Cast<ULSMineAwareMovementComponent>(Worker->GetCharacterMovement());
			Test.AddInfo(FString::Printf(TEXT("%s worker=%s position=%s velocity=%s HP=%.1f order=%d failure=%d tick=%d engaged=%s attackTarget=%s attackActive=%d attackConfigured=%d AIstatus=%d pathEnd=%s targetDistance=%.3f targetRange=%.3f clearStrike=%d mineConstrained=%d mineConstraintSeconds=%.3f"),
				*Label, *Worker->GetName(), *Worker->GetActorLocation().ToString(), *Worker->GetVelocity().ToString(), Health.CurrentHealth,
				static_cast<int32>(Worker->Orders->GetOrderKind()), static_cast<int32>(Worker->Orders->GetLastFailure()), Worker->Orders->IsComponentTickEnabled(),
				*GetNameSafe(Worker->Orders->GetEngagedTarget()), *GetNameSafe(Worker->BasicAttack->GetAttackTarget()), Worker->BasicAttack->IsActive(), Worker->BasicAttack->IsConfigured(),
				AI ? static_cast<int32>(AI->GetMoveStatus()) : -1, Path.IsValid() && !Path->GetPathPoints().IsEmpty() ? *Path->GetPathPoints().Last().Location.ToString() : TEXT("none"),
				Barricade.IsValid() ? ULSCombatTargetLibrary::GetAttackDistance(Worker.Get(), Barricade.Get()) : -1, Worker->BasicAttack->GetAttackSettings().Range,
				Barricade.IsValid() && Worker->BasicAttack->HasClearAttackPathFromLocation(Worker->GetActorLocation(), Barricade.Get()),
				Movement && Movement->IsMineConstrained(), Movement ? Movement->GetMineConstraintDuration() : -1));
		}
		Test.AddInfo(FString::Printf(TEXT("%s independentMine=%s valid=%d state=%d armed=%d coordinatorValid=%d"), *Label, *GetNameSafe(OtherMine.Get()), OtherMine.IsValid(),
			OtherMine.IsValid() ? static_cast<int32>(OtherMine->GetMineState()) : -1, OtherMine.IsValid() && OtherMine->IsArmed(), Encounter.IsValid()));
		for (TActorIterator<ALSEnemyTroop> It(World.Get()); It; ++It)
			if (Worker.IsValid() && FVector::Dist2D(It->GetActorLocation(), Worker->GetActorLocation()) < 1800)
				Test.AddInfo(FString::Printf(TEXT("%s nearbyNPC=%s position=%s state=%d dormant=%d behaviorTick=%d attackTarget=%s velocity=%s"), *Label,
					*It->GetActorLabel(), *It->GetActorLocation().ToString(), static_cast<int32>(It->Behavior->GetState()), It->Behavior->IsRegionallyDormant(),
					It->Behavior->IsComponentTickEnabled(), *GetNameSafe(It->BasicAttack->GetAttackTarget()), *It->GetVelocity().ToString()));
	}

	bool CheckIntendedCombatBays(UNavigationSystemV1* Nav, const ANavigationData* Data)
	{
		FAutomationTestBase& Test = *State->Test;
		if (!Test.TestEqual(TEXT("Combat bay terrain queries use logical Castle580 NavData"), Data->GetConfig().AgentRadius, CastleNavigationRadius)) return false;
		const FVector Entry(2500, 0, 0);
		const FVector Mouths[] = {FVector(4000, -6300, 0), FVector(4000, 0, 0), FVector(4000, 4600, 0)};
		const FVector Ends[] = {FVector(5200, -7200, 0), FVector(5200, -4600, 0), FVector(5200, -1000, 0), FVector(5400, 700, 0), FVector(5800, 5000, 0), FVector(5800, 3900, 0)};
		const TCHAR* LaneNames[] = {TEXT("Left"), TEXT("Center"), TEXT("Right")};
		const TCHAR* Names[] = {TEXT("Left lower firing bay"), TEXT("Left upper firing bay"), TEXT("Center lower holding bay"), TEXT("Center upper holding bay"), TEXT("Right offset bay"), TEXT("Right lower flank")};
		bool bValid = true;
		for (int32 Lane = 0; Lane < UE_ARRAY_COUNT(Mouths); ++Lane)
		{
			FNavLocation MouthPoint;
			const bool bMouthProjects = Nav->ProjectPointToNavigation(Mouths[Lane], MouthPoint, FVector(150, 150, 300), Data) && FVector::Dist2D(Mouths[Lane], MouthPoint.Location) <= 150;
			const FString MouthLabel = FString(LaneNames[Lane]) + TEXT(" physical mouth center has Castle580 logical projection within150cm");
			bValid &= Test.TestTrue(MouthLabel, bMouthProjects);
			if (!bMouthProjects)
			{
				FNavLocation Nearest;
				const bool bNearest = Nav->ProjectPointToNavigation(Mouths[Lane], Nearest, FVector(1200, 1200, 300), Data);
				Test.AddInfo(FString::Printf(TEXT("%s intended=%s diagnostic-only wide nearest=%s distance=%.1f; bay reachability has not been established because the mouth waypoint is blocked."),
					*MouthLabel, *Mouths[Lane].ToString(), bNearest ? *Nearest.Location.ToString() : TEXT("none"), bNearest ? FVector::Dist2D(Mouths[Lane], Nearest.Location) : -1));
				for (TActorIterator<ALSOutpost> It(World.Get()); It; ++It)
				{
					UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(It->GetRootComponent());
					if (!Body || FVector::Dist2D(It->GetActorLocation(), Mouths[Lane]) > 1200) continue;
					FVector Sample = Mouths[Lane], Closest;
					Sample.Z = Body->Bounds.Origin.Z;
					const float Distance = Body->GetClosestPointOnCollision(Sample, Closest);
					Test.AddInfo(FString::Printf(TEXT("Blocked mouth nearby facility=%s actor=%s component=%s bounds=%s navRelevant=%d closestBodyPoint=%s horizontalBodyDistance=%.1f"),
						*It->GetActorLabel(), *It->GetPathName(), *Body->GetName(), *Body->Bounds.GetBox().ToString(), Body->IsNavigationRelevant(), Distance >= 0 ? *Closest.ToString() : TEXT("none"), Distance >= 0 ? FVector::Dist2D(Sample, Closest) : -1));
				}
				continue;
			}
			FNavPathSharedPtr EntryPath;
			FString Failure;
			const bool bEntry = CompletePath(Nav, Data, Castle.Get(), Entry, Mouths[Lane], nullptr, &EntryPath, &Failure);
			bValid &= Test.TestTrue(FString(LaneNames[Lane]) + TEXT(" Castle580 complete J0-to-intended-mouth path: ") + Failure, bEntry);
			if (bEntry) bValid &= CheckLanePath(Test, FString(LaneNames[Lane]) + TEXT(" J0-to-mouth"), EntryPath, TArray<FVector>{Entry, Mouths[Lane]}, Entry, Mouths[Lane]);
			for (int32 Bay = Lane * 2; Bay < Lane * 2 + 2; ++Bay)
			{
				const FVector Approach = Bay == 1 ? FVector(5550, -6300, 0) : Mouths[Lane];
				if (Bay == 1)
				{
					FNavPathSharedPtr Maneuver;
					Failure.Reset();
					const bool bManeuver = CompletePath(Nav, Data, Castle.Get(), Mouths[Lane], Approach, nullptr, &Maneuver, &Failure);
					bValid &= Test.TestTrue(TEXT("Left upper bay physical500/Nav580 explicit mouth-to-gate-stage approach: ") + Failure, bManeuver);
					if (bManeuver) bValid &= CheckLanePath(Test, TEXT("Left upper bay mouth-to-gate-stage"), Maneuver, TArray<FVector>{Mouths[Lane], Approach}, Mouths[Lane], Approach);
				}
				FNavPathSharedPtr Path;
				Failure.Reset();
				const bool bComplete = CompletePath(Nav, Data, Castle.Get(), Approach, Ends[Bay], nullptr, &Path, &Failure);
				const FString Label = FString(Names[Bay]) + TEXT(" Castle580 complete intended-approach-to-bay path with <=150cm endpoint projection: ") + Failure;
				bValid &= Test.TestTrue(Label, bComplete);
				if (bComplete)
				{
					bValid &= CheckLanePath(Test, FString(Names[Bay]) + TEXT(" approach-to-bay"), Path, TArray<FVector>{Approach, Ends[Bay]}, Approach, Ends[Bay]);
					Test.AddInfo(FString::Printf(TEXT("%s intendedMouth=%s intendedApproach=%s intendedBay=%s projectedApproach=%s projectedBay=%s points=%d; terrain reachability only, live enemies may contest this space."), Names[Bay],
						*Mouths[Lane].ToString(), *Approach.ToString(), *Ends[Bay].ToString(), *Path->GetPathPoints()[0].Location.ToString(), *Path->GetPathPoints().Last().Location.ToString(), Path->GetPathPoints().Num()));
				}
			}
		}
		return bValid;
	}

	bool CheckNativeMouthWidths()
	{
		FAutomationTestBase& Test = *State->Test;
		const ALSBattlefieldSettings* Settings = FindOnlyActor<ALSBattlefieldSettings>(World.Get());
		if (!Test.TestNotNull(TEXT("Native mouth geometry has battlefield terrain settings"), Settings)) return false;
		const float Centers[] = {-6300, 0, 4600};
		const float Widths[] = {2800, 1600, 2200};
		const TCHAR* Names[] = {TEXT("Left"), TEXT("Center"), TEXT("Right")};
		bool bValid = true;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			double Lower = -TNumericLimits<double>::Max(), Upper = TNumericLimits<double>::Max();
			FString LowerOwner, UpperOwner;
			bool bCenterBlocked = false;
			for (AActor* Terrain : Settings->GetTerrainActors())
			{
				if (!IsValid(Terrain)) continue;
				TInlineComponentArray<UPrimitiveComponent*> Components(Terrain);
				for (const UPrimitiveComponent* Component : Components)
				{
					if (!Component->CanEverAffectNavigation() || !Component->IsNavigationRelevant() || Component->GetCollisionObjectType() == LSCollisionChannels::Ground) continue;
					const FBox Bounds = Component->Bounds.GetBox();
					if (Bounds.Min.X > 4000 || Bounds.Max.X < 4000 || Bounds.Min.Z > Settings->GetGroundHeight() + 100 || Bounds.Max.Z <= Settings->GetGroundHeight() + 100) continue;
					if (Bounds.Max.Y <= Centers[Index] && Bounds.Max.Y > Lower) { Lower = Bounds.Max.Y; LowerOwner = Terrain->GetActorLabel(); }
					else if (Bounds.Min.Y >= Centers[Index] && Bounds.Min.Y < Upper) { Upper = Bounds.Min.Y; UpperOwner = Terrain->GetActorLabel(); }
					else if (Bounds.Min.Y < Centers[Index] && Bounds.Max.Y > Centers[Index]) bCenterBlocked = true;
				}
			}
			const FString Label = FString(Names[Index]) + TEXT(" native rock mouth at X=4000cm");
			bValid &= Test.TestFalse(Label + TEXT(" center is free of solid terrain"), bCenterBlocked);
			if (!Test.TestTrue(Label + TEXT(" has actual rock boundaries on both sides"), !LowerOwner.IsEmpty() && !UpperOwner.IsEmpty())) { bValid = false; continue; }
			bValid &= Test.TestEqual(Label + TEXT(" authored clear width"), Upper - Lower, static_cast<double>(Widths[Index]), 1.0);
			Test.AddInfo(FString::Printf(TEXT("%s lower=%.1f (%s) upper=%.1f (%s) clearWidth=%.1fcm"), *Label, Lower, *LowerOwner, Upper, *UpperOwner, Upper - Lower));
		}
		return bValid;
	}
	void CheckRoutes(UNavigationSystemV1* Nav, const ANavigationData* Data)
	{
		FAutomationTestBase& Test = *State->Test;
		// 거점 중심은 충돌로 막혀 있으므로 이름 붙은 교차 지점의 통로 쪽을 검사한다.
		const FVector J0(2500, 0, 0), J2(18200, 0, 0), PreCore(18500, 1800, 0);
		const FVector LeftCross(10500, -6300, 0), CenterCross(10500, 0, 0), RightCross(10500, 4600, 0);
		const TArray<FVector> Routes[] = {
			{Castle->GetNavAgentLocation(), J0, FVector(3000, -6300, 0), FVector(4000, -6300, 0), FVector(4600, -7300, 0), LeftCross, FVector(15600, -5500, 0), J2, PreCore, FVector(20000, 1800, 0)},
			// C2 북서쪽 울타리·거점 사이는 반경 580cm Nav 에이전트가 통과하지 못하므로 남쪽 진입면을 기준점으로 쓴다.
			{Castle->GetNavAgentLocation(), J0, FVector(4000, 0, 0), FVector(4500, 0, 0), CenterCross, FVector(16100, -700, 0), J2, PreCore, FVector(20000, 1800, 0)},
			{Castle->GetNavAgentLocation(), J0, FVector(3000, 4600, 0), FVector(4000, 4600, 0), FVector(4500, 5100, 0), RightCross, FVector(12500, 5100, 0), FVector(13050, 4600, 0), FVector(15600, 3800, 0), J2, PreCore, FVector(20000, 1800, 0)},
			{LeftCross, CenterCross, RightCross}
		};
		const TCHAR* Names[] = {TEXT("L1-L2-preF"), TEXT("C1-C2-preF"), TEXT("R1-R2-preF"), TEXT("cross")};
		for (int32 RouteIndex = 0; RouteIndex < UE_ARRAY_COUNT(Routes); ++RouteIndex)
			for (int32 Segment = 1; Segment < Routes[RouteIndex].Num(); ++Segment)
			{
				const FVector& From = Routes[RouteIndex][Segment - 1];
				const FVector& To = Routes[RouteIndex][Segment];
				const FString Label = FString::Printf(TEXT("%s %s %s segment %d complete body-specific path"), *Preset.ToString(), *Data->GetConfig().Name.ToString(), Names[RouteIndex], Segment);
				FNavPathSharedPtr Forward, Reverse;
				FString Failure;
				const bool bForward = CompletePath(Nav, Data, Castle.Get(), From, To, nullptr, &Forward, &Failure);
				if (Test.TestTrue(Label + TEXT(" ") + Failure, bForward))
					CheckLanePath(Test, Label, Forward, BattlefieldCenterline(RouteIndex), From, To);
				Failure.Reset();
				const bool bReverse = CompletePath(Nav, Data, Castle.Get(), To, From, nullptr, &Reverse, &Failure);
				if (Test.TestTrue(Label + TEXT(" reverse ") + Failure, bReverse))
					CheckLanePath(Test, Label + TEXT(" reverse"), Reverse, BattlefieldCenterline(RouteIndex), To, From);
			}
	}
	void CheckVision()
	{
		FAutomationTestBase& Test = *State->Test;
		ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
		if (!Test.TestNotNull(TEXT("Real vision subsystem"), Vision)) return;
		Vision->RequestRefresh();
		Test.TestTrue(TEXT("Vision enabled in map PIE"), Vision->IsVisionEnabled());
		Test.TestTrue(TEXT("Sight snapshot exists"), Vision->GetSnapshotGeneration() > 0 && !Vision->GetSightCircles().IsEmpty());
		const TArray<FLSMinimapMarker> InitialMarkers = Vision->GetMinimapMarkers();
		Test.TestTrue(TEXT("Core public minimap position"), InitialMarkers.ContainsByPredicate([this](const FLSMinimapMarker& Marker)
			{ return Marker.bPublicPosition && Marker.Kind == ELSMinimapMarkerKind::Building && FVector::Dist2D(Marker.Location, Setup->CoreOutpost->GetActorLocation()) < 1; }));
		Test.TestTrue(TEXT("Friendly castle minimap marker"), InitialMarkers.ContainsByPredicate([this](const FLSMinimapMarker& Marker)
			{ return Marker.bFriendly && Marker.Kind == ELSMinimapMarkerKind::Castle && FVector::Dist2D(Marker.Location, Castle->GetActorLocation()) < 1; }));
		if (!Vision->IsPointVisible(Setup->CoreOutpost->GetActorLocation()))
		{
			Test.TestFalse(TEXT("Public Core marker does not reveal actor"), Vision->IsActorVisibleToFriendly(Setup->CoreOutpost));
			Test.TestFalse(TEXT("Public Core marker does not grant selection"), ULSEnemyVisionTestLibrary::TrySelect(Controller.Get(), Setup->CoreOutpost));
		}
		int32 MineCount = 0;
		for (TActorIterator<ALSMine> It(World.Get()); It; ++It)
		{
			++MineCount;
			Test.TestTrue(TEXT("Mine actor remains visible outside sight"), Vision->IsActorVisibleToFriendly(*It));
			if (!Vision->IsPointVisible(It->GetTriggerCenter()))
			{
				Test.TestFalse(TEXT("Unseen mine cannot be targeted through friendly sight"), ULSVisionSubsystem::PassesFriendlySight(Castle.Get(), *It));
				Test.TestFalse(TEXT("Undiscovered mine has no minimap marker"), InitialMarkers.ContainsByPredicate([&It](const FLSMinimapMarker& Marker)
					{ return Marker.ExplosionRadius > 0 && FVector::Dist2D(Marker.Location, It->GetTriggerCenter()) < 1; }));
			}
		}
		Test.TestEqual(TEXT("All 48 authored mines"), MineCount, 48);
		const float OriginalSight = Castle->Vision->GetSightRadius();
		Castle->Vision->SetSightRadius(50000);
		Vision->RequestRefresh();
		for (TActorIterator<ALSMine> It(World.Get()); It; ++It)
		{
			Test.TestTrue(TEXT("Discovered mine records blast radius on minimap"), Vision->GetMinimapMarkers().ContainsByPredicate([&It](const FLSMinimapMarker& Marker)
				{ return FMath::IsNearlyEqual(Marker.ExplosionRadius, It->GetExplosionRadius()) && FVector::Dist2D(Marker.Location, It->GetTriggerCenter()) < 1; }));
		}
		Castle->Vision->SetSightRadius(OriginalSight);
		Vision->RequestRefresh();
		for (TActorIterator<ALSMine> It(World.Get()); It; ++It)
			Test.TestTrue(TEXT("Discovered mine marker survives restored sight"), Vision->GetMinimapMarkers().ContainsByPredicate([&It](const FLSMinimapMarker& Marker)
				{ return Marker.ExplosionRadius > 0 && FVector::Dist2D(Marker.Location, It->GetTriggerCenter()) < 1; }));
	}
	void CheckProductionAndCoreApproach()
	{
		FAutomationTestBase& Test = *State->Test;
		if (!Test.TestTrue(TEXT("Public controller selection helper selects castle"), ULSEnemyVisionTestLibrary::TrySelect(Controller.Get(), Castle.Get()))) return;
		const int32 Population = GameState->GetCurrentPopulation();
		const int32 ActorCount = CountActors(World.Get());
		ALSTroopUnit* Spearman = nullptr;
		for (ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Archer, ELSTroopType::Cavalry})
		{
			const int32 Before = GameState->GetCurrentPopulation();
			Controller->ProduceTroop(Type);
			Test.TestEqual(TEXT("Controller produces requested troop"), Controller->GetLastProductionResult(), ELSProductionResult::Ready);
			ALSTroopUnit* Troop = Controller->Production->GetLastProducedUnit();
			if (!Test.TestNotNull(TEXT("Actual produced troop actor"), Troop)) continue;
			Test.TestEqual(TEXT("Produced troop type"), Troop->GetTroopType(), Type);
			Test.TestTrue(TEXT("Produced troop alive with initialized real stats"), Troop->IsStatsInitialized() && ULSCombatTargetLibrary::IsCombatAlive(Troop));
			Test.TestEqual(TEXT("Production increments population"), GameState->GetCurrentPopulation(), Before + 2);
			Test.TestEqual(TEXT("Production records both troops"), Controller->Production->GetLastProducedBatch().Num(), 2);
			for (const auto& Produced : Controller->Production->GetLastProducedBatch())
			{
				if (Produced.Get() != Troop || Type != ELSTroopType::Spearman) Produced->BasicAttack->EndBattleActions();
				FLSOrder Hold;
				Hold.Kind = ELSOrderKind::Hold;
				Produced->Orders->SubmitOrder(Hold);
			}
			if (Type == ELSTroopType::Spearman) { Spearman = Troop; Worker = Troop; }
		}
		Test.TestEqual(TEXT("All three troop types add population"), GameState->GetCurrentPopulation(), Population + 6);
		Test.TestTrue(TEXT("Production creates at least six real actors"), CountActors(World.Get()) >= ActorCount + 6);
		if (!Spearman) return;
		// 공개 공격 명령이 고르는 실제 접근 경로를 검사하고 장거리 이동은 수행하지 않는다.
		Spearman->Vision->SetSightRadius(50000);
		World->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
		Test.TestTrue(TEXT("Public selection helper selects produced Spearman"), ULSEnemyVisionTestLibrary::TrySelect(Controller.Get(), Spearman));
		const bool bAccepted = Controller->IssueSelectionOrder(ELSOrderKind::AttackTarget, Setup->CoreOutpost->GetActorLocation(), Setup->CoreOutpost, false);
		Test.TestTrue(TEXT("Public Core attack order finds approach"), bAccepted);
		AAIController* AI = Cast<AAIController>(Spearman->GetController());
		const FNavPathSharedPtr Path = AI && AI->GetPathFollowingComponent() ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
		if (Test.TestTrue(TEXT("Core public attack uses complete actual path"), Path.IsValid() && Path->IsValid() && !Path->IsPartial() && Path->GetPathPoints().Num() >= 2))
		{
			const FVector End = Path->GetPathPoints().Last().Location;
			const double Height = Spearman->GetActorLocation().Z - Spearman->GetNavAgentLocation().Z;
			Test.TestTrue(TEXT("Core approach is outside building center"), FVector::Dist2D(End, Setup->CoreOutpost->GetActorLocation()) > 100);
			Test.TestTrue(TEXT("Core approach reaches real melee range"), ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Spearman, Setup->CoreOutpost, End + FVector(0, 0, Height)) <= Spearman->BasicAttack->GetAttackSettings().Range);
		}
		Controller->HoldSelected();
	}
	void ApplyTerminalDamage()
	{
		AActor* Target = bDefeat ? static_cast<AActor*>(Castle.Get()) : static_cast<AActor*>(Setup->CoreOutpost.Get());
		ULSHealthComponent* Health = Target->FindComponentByClass<ULSHealthComponent>();
		FLSHealthSnapshot Snapshot;
		if (!State->Test->TestTrue(TEXT("Terminal target health initialized"), Health && Health->TryGetHealth(Snapshot))) return;
		FLSDamageRequest Damage;
		Damage.DamageAmount = Snapshot.MaxHealth * 2;
		State->Test->TestEqual(TEXT("Synthetic terminal damage accepted"), Health->ApplyDamage(Damage).Status, ELSApplyDamageStatus::Applied);
	}
	TSharedRef<FLSBattlefieldMapTestState> State;
	FName Preset;
	bool bDefeat = false;
	int32 Phase = 0;
	double PhaseStarted = FPlatformTime::Seconds();
	TUniquePtr<FStartPIEForAutomationCommand> Starter;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ALSBattlefieldSetup> Setup;
	TWeakObjectPtr<ALSGameState> GameState;
	TWeakObjectPtr<ALSPlayerController> Controller;
	TWeakObjectPtr<ALSCastle> Castle;
	FVector Home = FVector::ZeroVector;
	FVector HomeGround = FVector::ZeroVector;
	TMap<FName, FName> RegistryNames;
	TWeakObjectPtr<ALSTroopUnit> Worker;
	TWeakObjectPtr<ALSEnemyTroop> MeleeEnemy;
	TWeakObjectPtr<ALSEnemyTroop> WeaponTarget;
	TWeakObjectPtr<ALSTroopUnit> YieldTroop;
	FVector YieldStart = FVector::ZeroVector;
	bool bSawYield = false;
	double MinimumForwardDot = 1;
	int32 MovementRunIndex = 0;
	bool bReportedNavigation = false;
	double RepeatShotEnds = 0;
	FVector MovementDestination = FVector::ZeroVector;
	float WeaponTargetHealth = 0;
	FVector WeaponTargetOriginal = FVector::ZeroVector;
	float EncounterHealth = 0;
	int32 EncounterHits = 0;
	TWeakObjectPtr<ALSMine> Mine;
	TWeakObjectPtr<ALSMine> OtherMine;
	TWeakObjectPtr<ALSBarricade> Barricade;
	TArray<FName> BarricadeIds{TEXT("B_Gate_L1"), TEXT("B_Gate_L2"), TEXT("B_Gate_C1"), TEXT("B_Gate_C2"), TEXT("B_Gate_R1"), TEXT("B_Gate_R2"), TEXT("B_Gate_Core")};
	bool bOpeningGates = true;
	TWeakObjectPtr<ALSBattlefieldEncounterCoordinator> Encounter;
	TSet<FName> OpenedRegions;
	TMap<FName, TPair<FVector, FVector>> CastleGateOpenings;
	int32 BarricadeIndex = 0;
	TSharedRef<FLSBattlefieldObstacleObservation> ObstacleObservation = MakeShared<FLSBattlefieldObstacleObservation>();
	FDelegateHandle ObstacleDestroyedHandle;
	double DisarmStarted = 0;
	float DisarmDuration = 0;
	float BarricadeHealth = 0;
	float CoreHealth = 0;
	FVector CoreMeleeStart = FVector::ZeroVector;
	FVector BarricadeCrossFrom = FVector::ZeroVector;
	FVector BarricadeCrossTo = FVector::ZeroVector;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBattlefieldMapIntegrationTest,
	"Landship.Maps.Battlefield.RealMapEditorAndPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBattlefieldMapIntegrationTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Idle editor required"), GEditor && !GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress())) return false;
	UWorld* OriginalWorld = GEditor->GetEditorWorldContext().World();
	if (OriginalWorld)
		for (ULevel* Level : OriginalWorld->GetLevels())
			if (!TestFalse(TEXT("Test refuses to replace an editor map with unsaved changes"), Level->GetOutermost()->IsDirty())) return false;
	TSharedRef<FLSBattlefieldMapTestState> State = MakeShared<FLSBattlefieldMapTestState>();
	State->Test = this;
	if (OriginalWorld) FPackageName::DoesPackageExist(OriginalWorld->GetOutermost()->GetName(), &State->OriginalFilename);
	if (!TestTrue(TEXT("Real battlefield map exists on disk"), FPackageName::DoesPackageExist(BattlefieldMap, &State->SourceFilename))) return false;
	State->SourceHash = FMD5Hash::HashFile(*State->SourceFilename);
	const FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	State->Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/BattlefieldMap") / RunId) + TEXT("/");
	State->MountRoot = TEXT("/LSBattlefieldAutomation_") + RunId + TEXT("/");
	State->CopyFilename = State->Directory / TEXT("L_BattlefieldTest.umap");
	IFileManager::Get().MakeDirectory(*State->Directory, true);
	FPackageName::RegisterMountPoint(State->MountRoot, State->Directory);
	// 템플릿으로 읽은 복제본만 바꾸고 저장한다. 원본 맵 경로는 SaveMap에 넘기지 않는다.
	if (TestTrue(TEXT("Load actual battlefield map as editor template"), FEditorFileUtils::LoadMap(State->SourceFilename, true, false)))
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		State->EditorCopy = World;
		ALSBattlefieldLayoutAuthoring* Layout = FindOnlyActor<ALSBattlefieldLayoutAuthoring>(World);
		if (TestTrue(TEXT("Template uses a distinct package"), World->GetOutermost()->GetName() != BattlefieldMap) && ValidateMap(*this, World) && Layout)
		{
			TestEqual(TEXT("Exactly 368 authored registry actors"), Layout->Registry.Num(), 368);
			CheckPresetDefinitions(*this, Layout);
			const FLSBattlefieldSnapshot Before = CaptureRegistry(Layout);
			const int32 ActorCount = CountActors(World);
			for (const FName PresetId : {FName(TEXT("V0"))})
			{
				Layout->SelectedPreset = PresetId;
				const FLSBattlefieldLayoutPreset* Preset = Layout->Presets.FindByPredicate([PresetId](const FLSBattlefieldLayoutPreset& Candidate) { return Candidate.PresetId == PresetId; });
				if (!TestNotNull(TEXT("Authored preset exists"), Preset)) continue;
				Layout->ApplySelectedPreset();
				CheckRegistry(*this, Layout, Before, Preset);
				TestEqual(TEXT("Preset does not create or destroy actors"), CountActors(World), ActorCount);
				ValidateMap(*this, World);
			}
			if (TestTrue(TEXT("Save template only to unique temporary map"), FEditorFileUtils::SaveMap(World, State->CopyFilename)))
			{
				const FLSBattlefieldSnapshot Saved = CaptureRegistry(Layout);
				if (TestTrue(TEXT("Reload saved temporary map"), FEditorFileUtils::LoadMap(State->CopyFilename, false, false)))
				{
					World = GEditor->GetEditorWorldContext().World();
					State->EditorCopy = World;
					Layout = FindOnlyActor<ALSBattlefieldLayoutAuthoring>(World);
					if (ValidateMap(*this, World) && Layout)
					{
						CheckRegistry(*this, Layout, Saved);
						CheckPresetDefinitions(*this, Layout);
						TestEqual(TEXT("Reload preserves selected preset"), Layout->SelectedPreset, FName(TEXT("V0")));
						State->bReady = true;
					}
				}
			}
		}
	}
	for (FName Preset : {FName(TEXT("V0"))})
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBattlefieldPIECommand>(State, Preset));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBattlefieldPIECommand>(State, FName(TEXT("V0")), true));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FLSBattlefieldCleanupCommand>(State));
	return true;
}

#endif
