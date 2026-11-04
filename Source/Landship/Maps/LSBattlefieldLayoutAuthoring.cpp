#include "Maps/LSBattlefieldLayoutAuthoring.h"
#include "Maps/LSBattlefieldEncounterCoordinator.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/DataValidation.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogLSBattlefieldLayout, Log, All);

#if WITH_EDITOR
namespace
{
void NotifyLayoutResult(const FText& Message, bool bSuccess)
{
	if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}
}
#endif

ALSBattlefieldLayoutAuthoring::ALSBattlefieldLayoutAuthoring()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bIsEditorOnlyActor = true;
}

bool ALSBattlefieldLayoutAuthoring::IsEditorOnly() const
{
	return true;
}

bool ALSBattlefieldLayoutAuthoring::CanApplyPreset(FName PresetId, FString& OutError) const
{
	OutError.Reset();
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::Editor)
	{
		OutError = TEXT("Layout presets can only be applied in an editor world.");
		return false;
	}
	if (GEditor && (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress()))
	{
		OutError = TEXT("Layout presets cannot be applied while PIE/SIE is running or queued.");
		return false;
	}
	return ValidateConfiguration(PresetId, OutError);
#else
	OutError = TEXT("Layout presets are editor-only.");
	return false;
#endif
}

bool ALSBattlefieldLayoutAuthoring::ValidateConfiguration(FName PresetId, FString& OutError) const
{
	OutError.Reset();
	const UWorld* World = GetWorld();
	if (Registry.IsEmpty())
	{
		OutError = TEXT("Registry must contain at least one actor.");
		return false;
	}
	TSet<FName> ActorIds;
	TSet<const AActor*> Actors;
	for (const FLSBattlefieldLayoutActorEntry& Entry : Registry)
	{
		if (Entry.ActorId.IsNone() || ActorIds.Contains(Entry.ActorId))
		{
			OutError = FString::Printf(TEXT("Registry ActorId '%s' is empty or duplicated."), *Entry.ActorId.ToString());
			return false;
		}
		ActorIds.Add(Entry.ActorId);
		const AActor* Actor = Entry.Actor.Get();
		if (!IsValid(Actor) || Actor == this || Actor->GetWorld() != World || Actors.Contains(Actor))
		{
			OutError = FString::Printf(TEXT("Registry actor '%s' must be valid, unique, in the same world, and not the authoring actor."), *Entry.ActorId.ToString());
			return false;
		}
		if (!IsValid(Actor->GetRootComponent()))
		{
			OutError = FString::Printf(TEXT("Registry actor '%s' needs a valid root component to apply a transform."), *Entry.ActorId.ToString());
			return false;
		}
		TArray<AActor*> AttachedActors;
		Actor->GetAttachedActors(AttachedActors);
		if (Actor->GetAttachParentActor() || !AttachedActors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Registry actor '%s' must not have actor attachment relationships."), *Entry.ActorId.ToString());
			return false;
		}
		Actors.Add(Actor);
	}

	TSet<FName> PresetIds;
	TSet<FName> FixedIds;
	for (const FName FixedId : FixedActorIds)
	{
		if (!ActorIds.Contains(FixedId) || FixedIds.Contains(FixedId))
		{
			OutError = TEXT("FixedActorIds must name unique registered actors.");
			return false;
		}
		FixedIds.Add(FixedId);
	}
	if (EncounterCoordinator && (!IsValid(EncounterCoordinator) || EncounterCoordinator->GetWorld() != World))
	{
		OutError = TEXT("Encounter coordinator must be valid and in the same world.");
		return false;
	}
	for (const FLSBattlefieldLayoutPreset& Preset : Presets)
	{
		if (Preset.PresetId.IsNone() || PresetIds.Contains(Preset.PresetId))
		{
			OutError = FString::Printf(TEXT("PresetId '%s' is empty or duplicated."), *Preset.PresetId.ToString());
			return false;
		}
		PresetIds.Add(Preset.PresetId);
		if (Preset.Placements.Num() != ActorIds.Num())
		{
			OutError = FString::Printf(TEXT("Preset '%s' must contain exactly every registered ActorId."), *Preset.PresetId.ToString());
			return false;
		}
		for (const TPair<FName, FTransform>& Placement : Preset.Placements)
		{
			if (!ActorIds.Contains(Placement.Key))
			{
				OutError = FString::Printf(TEXT("Preset '%s' contains unregistered ActorId '%s'."), *Preset.PresetId.ToString(), *Placement.Key.ToString());
				return false;
			}
			const FVector Scale = Placement.Value.GetScale3D();
			if (!Placement.Value.IsValid() || Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0)
			{
				OutError = FString::Printf(TEXT("Preset '%s', ActorId '%s' needs a finite valid transform and positive nonzero scale."), *Preset.PresetId.ToString(), *Placement.Key.ToString());
				return false;
			}
		}
		TMap<FName, FTransform> ActorTransforms;
		for (const FLSBattlefieldLayoutActorEntry& Entry : Registry)
		{
			const FTransform& Transform = Preset.Placements.FindChecked(Entry.ActorId);
			if (FixedIds.Contains(Entry.ActorId) && !Transform.Equals(Presets[0].Placements.FindChecked(Entry.ActorId)))
			{
				OutError = FString::Printf(TEXT("Preset '%s' moves fixed actor '%s'."), *Preset.PresetId.ToString(), *Entry.ActorId.ToString());
				return false;
			}
			ActorTransforms.Add(Entry.Actor->GetFName(), Transform);
		}
		if (EncounterCoordinator && !EncounterCoordinator->ValidatePresetTransforms(ActorTransforms, OutError)) return false;
	}
	if (PresetId.IsNone() || !PresetIds.Contains(PresetId))
	{
		OutError = FString::Printf(TEXT("Selected preset '%s' does not exist."), *PresetId.ToString());
		return false;
	}
	return true;
}

FString ALSBattlefieldLayoutAuthoring::GetConfigurationError() const
{
	FString Error;
	ValidateConfiguration(SelectedPreset, Error);
	return Error;
}

TArray<FString> ALSBattlefieldLayoutAuthoring::GetPresetOptions() const
{
	TArray<FString> Options;
	for (const FLSBattlefieldLayoutPreset& Preset : Presets)
	{
		if (!Preset.PresetId.IsNone())
		{
			Options.AddUnique(Preset.PresetId.ToString());
		}
	}
	return Options;
}

FName ALSBattlefieldLayoutAuthoring::GetMatchingPreset() const
{
	FString Error;
	if (Presets.IsEmpty() || !ValidateConfiguration(Presets[0].PresetId, Error))
	{
		return NAME_None;
	}
	for (const FLSBattlefieldLayoutPreset& Preset : Presets)
	{
		bool bMatches = true;
		for (const FLSBattlefieldLayoutActorEntry& Entry : Registry)
		{
			// 다른 액터에 부착하지 않은 루트의 상대 변환은 월드 변환과 같다. 미개방 맵에서는 월드 변환 캐시가 초기화되지 않으므로 저장된 상대 변환을 읽는다.
			if (!Entry.Actor->GetRootComponent()->GetRelativeTransform().Equals(Preset.Placements.FindChecked(Entry.ActorId)))
			{
				bMatches = false;
				break;
			}
		}
		if (bMatches)
		{
			return Preset.PresetId;
		}
	}
	return NAME_None;
}

void ALSBattlefieldLayoutAuthoring::ValidateLayout()
{
	const FName MatchingPreset = GetMatchingPreset();
	const FString Current = MatchingPreset.IsNone() ? TEXT("Custom") : MatchingPreset.ToString();
	UE_LOG(LogLSBattlefieldLayout, Log, TEXT("%s: current layout='%s', selected preset='%s'."), *GetPathName(), *Current, *SelectedPreset.ToString());
	if (MatchingPreset != SelectedPreset)
	{
		UE_LOG(LogLSBattlefieldLayout, Warning, TEXT("Selected preset '%s' does not match the current layout '%s'. Selection alone does not apply a preset."), *SelectedPreset.ToString(), *Current);
	}
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty())
	{
		UE_LOG(LogLSBattlefieldLayout, Warning, TEXT("%s: %s"), *GetPathName(), *Error);
		return;
	}
	UE_LOG(LogLSBattlefieldLayout, Log, TEXT("%s: layout configuration is valid."), *GetPathName());
}

void ALSBattlefieldLayoutAuthoring::ApplySelectedPreset()
{
	FString Error;
	if (!CanApplyPreset(SelectedPreset, Error))
	{
		UE_LOG(LogLSBattlefieldLayout, Warning, TEXT("%s: %s No actors changed."), *GetPathName(), *Error);
#if WITH_EDITOR
		NotifyLayoutResult(FText::FromString(FString::Printf(TEXT("배치안 '%s' 적용 실패: %s"), *SelectedPreset.ToString(), *Error)), false);
#endif
		return;
	}
#if WITH_EDITOR
	const FLSBattlefieldLayoutPreset& Preset = *Presets.FindByPredicate(
		[this](const FLSBattlefieldLayoutPreset& Candidate) { return Candidate.PresetId == SelectedPreset; });
	const FScopedTransaction Transaction(NSLOCTEXT("LSBattlefieldLayout", "ApplyPreset", "전장 배치안 적용"));
	for (const FLSBattlefieldLayoutActorEntry& Entry : Registry)
	{
		Entry.Actor->Modify();
		Entry.Actor->GetRootComponent()->Modify();
		Entry.Actor->SetActorTransform(Preset.Placements.FindChecked(Entry.ActorId), false, nullptr, ETeleportType::TeleportPhysics);
	}
	UE_LOG(LogLSBattlefieldLayout, Log, TEXT("%s: applied preset '%s' to %d actors."), *GetPathName(), *SelectedPreset.ToString(), Registry.Num());
	NotifyLayoutResult(FText::FromString(FString::Printf(TEXT("배치안 '%s'을 액터 %d개에 적용했다."), *SelectedPreset.ToString(), Registry.Num())), true);
#endif
}

#if WITH_EDITOR
EDataValidationResult ALSBattlefieldLayoutAuthoring::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult BaseResult = Super::IsDataValid(Context);
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty())
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return BaseResult == EDataValidationResult::Invalid ? BaseResult : EDataValidationResult::Valid;
}
#endif
