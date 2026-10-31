#include "Vision/LSVisionSubsystem.h"

#include "Combat/LSFactionInterface.h"
#include "Combat/LSHealthComponent.h"
#include "Vision/LSVisionGeometry.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionPresentation.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "EngineUtils.h"

bool ULSVisionSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULSVisionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldStarted = true;
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(&InWorld);
	bVisionEnabled = Settings != nullptr;
	if (!Settings)
	{
		int32 SettingsCount = 0;
		for (TActorIterator<ALSBattlefieldSettings> It(&InWorld); It; ++It)
		{
			++SettingsCount;
		}
		if (SettingsCount > 0)
		{
			UE_LOGFMT(LogTemp, Warning,
					  "[{Function}] Battlefield vision disabled: expected one valid settings actor, found "
					  "{Count}. Validate bounds, rendering parameters and TerrainActors.",
					  ("Function", __FUNCTION__), ("Count", SettingsCount));
		}
	}
	if (Settings)
	{
		RefreshInterval = Settings->VisionRefreshInterval;
		for (AActor* Missing : Settings->FindUnconfiguredParticipants())
		{
			UE_LOGFMT(LogTemp, Warning, "[{Function}] Missing vision participant: {Actor}",
					  ("Function", __FUNCTION__), ("Actor", Missing->GetPathName()));
		}
	}
	RequestRefresh();
	if (bVisionEnabled && FApp::CanEverRender() && InWorld.GetGameViewport() &&
		!ALSVisionPresentation::Find(&InWorld))
	{
		InWorld.SpawnActor<ALSVisionPresentation>();
	}
}

TStatId ULSVisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULSVisionSubsystem, STATGROUP_Tickables);
}

void ULSVisionSubsystem::Tick(float DeltaTime)
{
	if (!bWorldStarted || !bVisionEnabled || !GetWorld() || GetWorld()->IsPaused())
	{
		return;
	}
	Elapsed += DeltaTime;
	if (bRefreshPending || Elapsed >= RefreshInterval)
	{
		Elapsed = FMath::Fmod(Elapsed, RefreshInterval);
		Refresh();
	}
}

void ULSVisionSubsystem::RegisterParticipant(ULSVisionParticipantComponent* Participant)
{
	Participants.AddUnique(Participant);
	if (Participant->bPublicCorePosition && !PublicCoreMarkers.Contains(Participant->GetOwner()))
	{
		FLSMinimapMarker Marker;
		Marker.Location = Participant->GetOwner()->GetActorLocation();
		Marker.Kind = Participant->MarkerKind;
		Marker.bPublicPosition = true;
		PublicCoreMarkers.Add(Participant->GetOwner(), Marker);
	}
	RequestRefresh();
}

void ULSVisionSubsystem::UnregisterParticipant(ULSVisionParticipantComponent* Participant)
{
	Participants.Remove(Participant);
	if (Participant && Participant->GetOwner())
	{
		VisibleActors.Remove(Participant->GetOwner());
	}
	RequestRefresh();
}

void ULSVisionSubsystem::RequestRefresh()
{
	bRefreshPending = true;
	if (bWorldStarted && bVisionEnabled && !bRefreshing && GetWorld() && !GetWorld()->IsPaused() &&
		!GetWorld()->bIsTearingDown)
	{
		Refresh();
	}
}

bool ULSVisionSubsystem::IsPointVisible(FVector Location) const
{
	for (const FLSSightCircle& Circle : SightCircles)
	{
		if (LSVisionGeometry::IsInsideSight(FVector2D(Location), Circle.Center, Circle.Radius))
		{
			return true;
		}
	}
	return false;
}

bool ULSVisionSubsystem::IsActorVisibleToFriendly(const AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetWorld() != GetWorld())
	{
		return false;
	}
	if (!bVisionEnabled)
	{
		return true;
	}
	const ULSVisionParticipantComponent* Participant =
		Actor->FindComponentByClass<ULSVisionParticipantComponent>();
	// 시야에 참여하지 않는 기존 전투 대역·지형은 가시성 소유권을 갖지 않는다.
	return !Participant || !Participant->ShouldConceal() ||
		   VisibleActors.Contains(const_cast<AActor*>(Actor));
}

bool ULSVisionSubsystem::PassesFriendlySight(const AActor* Observer, const AActor* Target)
{
	if (!IsValid(Observer) || !IsValid(Target))
	{
		return false;
	}
	const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Observer);
	if (!Faction || Faction->GetFaction() != ELSFaction::Ally)
	{
		return true;
	}
	const UWorld* World = Observer->GetWorld();
	const ULSVisionSubsystem* Vision = World ? World->GetSubsystem<ULSVisionSubsystem>() : nullptr;
	// 에디터 미리보기에는 플레이 시야가 없다. 게임 월드에서는 서브시스템이 생성된다.
	return !Vision || Vision->IsActorVisibleToFriendly(Target);
}

void ULSVisionSubsystem::GetConcealedActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	if (!bVisionEnabled)
	{
		return;
	}
	for (const auto& Weak : Participants)
	{
		if (const ULSVisionParticipantComponent* Participant = Weak.Get())
		{
			if (!IsActorVisibleToFriendly(Participant->GetOwner()))
			{
				OutActors.Add(Participant->GetOwner());
			}
		}
	}
}

TArray<FLSMinimapMarker> ULSVisionSubsystem::GetMinimapMarkers() const
{
	return MinimapMarkers;
}

void ULSVisionSubsystem::MarkDiscoveryCaptionDisplayed()
{
	if (bEnemyDiscovered)
	{
		bDiscoveryCaptionDisplayed = true;
	}
}

void ULSVisionSubsystem::Refresh()
{
	if (bRefreshing || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshing, true);
	bRefreshPending = false;
	Participants.RemoveAll([](const auto& Weak) { return !Weak.IsValid() || Weak->IsBeingDestroyed(); });
	TArray<FLSSightCircle> NewCircles;
	for (const auto& Weak : Participants)
	{
		const ULSVisionParticipantComponent* Participant = Weak.Get();
		if (Participant->bProvidesSight && Participant->IsFriendly() && Participant->IsLivingCombatant())
		{
			NewCircles.Add(
				{FVector2D(Participant->GetOwner()->GetActorLocation()), Participant->GetSightRadius()});
		}
	}
	TSet<TWeakObjectPtr<AActor>> NewVisible;
	TArray<FLSMinimapMarker> NewMarkers;
	PublicCoreMarkers.GenerateValueArray(NewMarkers);
	bool bFoundEnemy = false;
	const TArray<TWeakObjectPtr<ULSVisionParticipantComponent>> Current = Participants;
	for (const auto& Weak : Current)
	{
		ULSVisionParticipantComponent* Participant = Weak.Get();
		if (!IsValid(Participant) || !IsValid(Participant->GetOwner()) ||
			Participant->GetOwner()->IsActorBeingDestroyed())
		{
			continue;
		}
		AActor* Actor = Participant->GetOwner();
		bool bVisible = !Participant->ShouldConceal();
		for (const FLSSightCircle& Circle : NewCircles)
		{
			bVisible |= LSVisionGeometry::IsInsideSight(FVector2D(Actor->GetActorLocation()), Circle.Center,
														Circle.Radius);
		}
		if (bVisible)
		{
			NewVisible.Add(Actor);
			bFoundEnemy |= Participant->IsEnemyDiscoveryCandidate();
		}
		const bool bHasHealth = Actor->FindComponentByClass<ULSHealthComponent>() != nullptr;
		if (!Participant->bPublicCorePosition && bVisible &&
			(!bHasHealth || Participant->IsLivingCombatant()))
		{
			FLSMinimapMarker Marker;
			Marker.Location = Actor->GetActorLocation();
			Marker.Kind = Participant->MarkerKind;
			Marker.bFriendly = Participant->IsFriendly();
			Marker.bPublicPosition = Participant->bPublicCorePosition;
			NewMarkers.Add(Marker);
		}
	}
	SightCircles = MoveTemp(NewCircles);
	VisibleActors = MoveTemp(NewVisible);
	MinimapMarkers = MoveTemp(NewMarkers);
	++SnapshotGeneration;
	for (const auto& Weak : Current)
	{
		if (ULSVisionParticipantComponent* Participant = Weak.Get())
		{
			Participant->ApplySightVisibility(IsActorVisibleToFriendly(Participant->GetOwner()));
		}
	}
	const bool bFirstDiscovery = bFoundEnemy && !bEnemyDiscovered;
	bEnemyDiscovered |= bFoundEnemy;
	OnSnapshotCommittedEvent.Broadcast();
	if (bFirstDiscovery && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		OnFirstEnemyDiscoveredEvent.Broadcast();
	}
}

void ULSVisionSubsystem::Deinitialize()
{
	bWorldStarted = false;
	Participants.Reset();
	SightCircles.Reset();
	VisibleActors.Reset();
	MinimapMarkers.Reset();
	PublicCoreMarkers.Reset();
	Super::Deinitialize();
}
