#include "Vision/LSVisionPresentation.h"

#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionGeometry.h"
#include "Vision/LSVisionSubsystem.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CanvasItem.h"
#include "Logging/StructuredLog.h"
#include "TimerManager.h"

ALSVisionPresentation::ALSVisionPresentation()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	TerrainCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("TerrainCapture"));
	TerrainCapture->SetupAttachment(RootComponent);
	TerrainCapture->bCaptureEveryFrame = false;
	TerrainCapture->bCaptureOnMovement = false;
	TerrainCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	TerrainCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// 조명·그림자와 무관한 지형 색만 담아 숨은 적의 그림자를 배경에 굽지 않는다.
	TerrainCapture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	FogPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("FogPostProcess"));
	FogPostProcess->SetupAttachment(RootComponent);
	FogPostProcess->bUnbound = true;
}

ALSVisionPresentation* ALSVisionPresentation::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<ALSVisionPresentation> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void ALSVisionPresentation::BeginPlay()
{
	Super::BeginPlay();
	Settings = ALSBattlefieldSettings::Find(GetWorld());
	if (!Settings || !FApp::CanEverRender())
	{
		return;
	}
	const FVector2D Extent = Settings->GetWorldBounds().GetSize();
	TextureWidth = Settings->VisibilityTextureWidth;
	OutsideDarkness = Settings->OutsideDarkness;
	const int32 Height = FMath::Clamp(FMath::RoundToInt(TextureWidth * Extent.X / Extent.Y), 64, 2048);
	TerrainTexture = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureWidth, Height, RTF_RGBA8,
																   FLinearColor::Black);
	VisibilityTexture = UKismetRenderingLibrary::CreateRenderTarget2D(this, TextureWidth, Height, RTF_RGBA8,
																	  FLinearColor(0, 0, 0, OutsideDarkness));
	VisibilityTexture->AddressX = TA_Clamp;
	VisibilityTexture->AddressY = TA_Clamp;
	VisibilityTexture->UpdateResourceImmediate(true);
	if (Settings->FogMaterial)
	{
		FogInstance = UMaterialInstanceDynamic::Create(Settings->FogMaterial, this);
		FogInstance->SetTextureParameterValue(TEXT("VisibilityMask"), VisibilityTexture);
		FogInstance->SetScalarParameterValue(TEXT("OutsideDarkness"), OutsideDarkness);
		const FBox2D Bounds = Settings->GetWorldBounds();
		FogInstance->SetVectorParameterValue(TEXT("BattlefieldBounds"),
											 FLinearColor(Bounds.Min.X, Bounds.Min.Y, Extent.X, Extent.Y));
		FogPostProcess->AddOrUpdateBlendable(FogInstance, 1.0f);
	}
	else
	{
		UE_LOGFMT(LogTemp, Warning, "[{Function}] Battlefield fog material is missing: {Map}",
				  ("Function", __FUNCTION__), ("Map", GetWorld()->GetMapName()));
	}
	if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
	{
		Vision->OnSnapshotCommittedEvent.AddDynamic(this, &ALSVisionPresentation::UpdateVisibility);
		UpdateVisibility();
	}
	// 초기 액터의 렌더 상태가 준비된 뒤 한 번 캡처한다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ALSVisionPresentation::CaptureTerrain);
}

void ALSVisionPresentation::CaptureTerrain()
{
	if (!Settings || !TerrainTexture)
	{
		return;
	}
	TerrainCapture->ShowOnlyActors.Reset();
	for (AActor* Terrain : Settings->GetTerrainActors())
	{
		if (IsValid(Terrain))
		{
			TerrainCapture->ShowOnlyActors.Add(Terrain);
		}
	}
	if (TerrainCapture->ShowOnlyActors.IsEmpty())
	{
		UE_LOGFMT(LogTemp, Warning, "[{Function}] Battlefield terrain whitelist is empty: {Map}",
				  ("Function", __FUNCTION__), ("Map", GetWorld()->GetMapName()));
		return;
	}
	const FBox2D Bounds = Settings->GetWorldBounds();
	const FVector2D Center = Bounds.GetCenter();
	TerrainCapture->SetWorldLocation(FVector(Center.X, Center.Y, Settings->GetGroundHeight() + 5000.0));
	TerrainCapture->SetWorldRotation(FRotator(-90, 0, 0));
	TerrainCapture->OrthoWidth = Bounds.GetSize().Y;
	TerrainCapture->TextureTarget = TerrainTexture;
	TerrainCapture->CaptureScene();
}

void ALSVisionPresentation::UpdateVisibility()
{
	const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	if (!Vision || !Settings || !VisibilityTexture)
	{
		return;
	}
	UKismetRenderingLibrary::ClearRenderTarget2D(this, VisibilityTexture,
												 FLinearColor(0, 0, 0, OutsideDarkness));
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, VisibilityTexture, Canvas, Size, Context);
	if (Canvas && Canvas->Canvas)
	{
		const FBox2D Bounds = Settings->GetWorldBounds();
		for (const FLSSightCircle& Circle : Vision->GetSightCircles())
		{
			FVector2D UV;
			if (!LSVisionGeometry::WorldToUV(Circle.Center, Bounds, UV) || Circle.Radius <= 0)
			{
				continue;
			}
			const FVector2D Radii(Circle.Radius * Size.X / Bounds.GetSize().Y,
								  Circle.Radius * Size.Y / Bounds.GetSize().X);
			// 각 원을 불투명하게 덮어써 겹친 영역도 같은 투명도를 유지한다.
			FCanvasNGonItem Disc(UV * Size, Radii, 128, FLinearColor::Transparent);
			Disc.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Disc);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void ALSVisionPresentation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
		{
			Vision->OnSnapshotCommittedEvent.RemoveDynamic(this, &ALSVisionPresentation::UpdateVisibility);
		}
	}
	Super::EndPlay(EndPlayReason);
}
