#include "Maps/LSBattlefieldSettings.h"

#include "EngineUtils.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ALSBattlefieldSettings::ALSBattlefieldSettings()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	BoundsPreview = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("BoundsPreview"));
	if (BoundsPreview)
	{
		BoundsPreview->SetupAttachment(RootComponent);
		BoundsPreview->SetIsVisualizationComponent(true);
		BoundsPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoundsPreview->SetCanEverAffectNavigation(false);
		BoundsPreview->SetHiddenInGame(true);
		BoundsPreview->bDrawOnlyIfSelected = true;
		BoundsPreview->ShapeColor = FColor::Cyan;
	}
#endif
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FogAsset(
		TEXT("/Game/Landship/Vision/Materials/M_BattlefieldFog.M_BattlefieldFog"));
	FogMaterial = FogAsset.Object;
}

bool ALSBattlefieldSettings::HasValidSettings() const
{
	const FVector2D Size = BoundsMax - BoundsMin;
	if (!FMath::IsFinite(BoundsMin.X) || !FMath::IsFinite(BoundsMin.Y) || !FMath::IsFinite(BoundsMax.X) ||
		!FMath::IsFinite(BoundsMax.Y) || !FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) ||
		Size.X <= 0.0 || Size.Y <= 0.0 || !FMath::IsFinite(GroundHeight))
	{
		return false;
	}
	if (!FMath::IsFinite(VisionRefreshInterval) || VisionRefreshInterval <= 0 ||
		VisibilityTextureWidth < 64 || VisibilityTextureWidth > 2048 || !FMath::IsFinite(OutsideDarkness) ||
		OutsideDarkness < 0 || OutsideDarkness > 1)
	{
		return false;
	}
	for (const TObjectPtr<AActor>& TerrainActor : TerrainActors)
	{
		if (!IsValid(TerrainActor.Get()) || TerrainActor->GetWorld() != GetWorld() ||
			TerrainActor->FindComponentByClass<ULSVisionParticipantComponent>())
		{
			return false;
		}
	}
	return true;
}

TArray<AActor*> ALSBattlefieldSettings::FindUnconfiguredParticipants() const
{
	TArray<AActor*> Missing;
	if (!GetWorld())
	{
		return Missing;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(*It);
		bool bNeedsParticipant = Faction && Faction->GetFaction() == ELSFaction::Enemy;
		TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			bNeedsParticipant |= Primitive->GetCollisionObjectType() == LSCollisionChannels::PathObstacle;
		}
		if (bNeedsParticipant && !It->FindComponentByClass<ULSVisionParticipantComponent>())
		{
			Missing.Add(*It);
		}
	}
	return Missing;
}

ALSBattlefieldSettings* ALSBattlefieldSettings::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	ALSBattlefieldSettings* Found = nullptr;
	for (TActorIterator<ALSBattlefieldSettings> It(World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		if (Found)
		{
			return nullptr;
		}
		Found = *It;
	}
	return Found && Found->HasValidSettings() ? Found : nullptr;
}

#if WITH_EDITOR
void ALSBattlefieldSettings::UpdateBoundsPreview()
{
	if (!BoundsPreview)
	{
		return;
	}
	const FVector2D Size = BoundsMax - BoundsMin;
	const bool bValid = FMath::IsFinite(BoundsMin.X) && FMath::IsFinite(BoundsMin.Y) &&
						FMath::IsFinite(BoundsMax.X) && FMath::IsFinite(BoundsMax.Y) &&
						FMath::IsFinite(GroundHeight) && FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) &&
						Size.X > 0 && Size.Y > 0;
	BoundsPreview->SetVisibility(bValid);
	if (bValid)
	{
		const FVector2D Center = BoundsMin + Size * 0.5;
		BoundsPreview->SetWorldLocation(FVector(Center.X, Center.Y, GroundHeight));
		BoundsPreview->SetWorldRotation(FRotator::ZeroRotator);
		BoundsPreview->SetWorldScale3D(FVector::OneVector);
		BoundsPreview->SetBoxExtent(FVector(Size.X * 0.5, Size.Y * 0.5, 20));
	}
}

void ALSBattlefieldSettings::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UpdateBoundsPreview();
}

void ALSBattlefieldSettings::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateBoundsPreview();
}

EDataValidationResult ALSBattlefieldSettings::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult BaseResult = Super::IsDataValid(Context);
	bool bInvalid = BaseResult == EDataValidationResult::Invalid;
	const TArray<AActor*> MissingParticipants = FindUnconfiguredParticipants();
	for (const AActor* Missing : MissingParticipants)
	{
		Context.AddError(FText::FromString(
			FString::Printf(TEXT("시야 참여 설정이 없는 적 또는 장애물: %s"), *Missing->GetPathName())));
		bInvalid = true;
	}
	if (GetWorld() && Find(GetWorld()) != this)
	{
		Context.AddError(FText::FromString(TEXT("전장 설정은 월드에 유효한 인스턴스 하나만 있어야 합니다.")));
		bInvalid = true;
	}
	if (!HasValidSettings())
	{
		Context.AddError(
			FText::FromString(TEXT("전장 경계는 유한한 양의 크기여야 하며 지면 높이는 유한해야 합니다. 지형 "
								   "목록에는 같은 월드의 유효한 액터만 지정하세요.")));
		return EDataValidationResult::Invalid;
	}
	return bInvalid ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
