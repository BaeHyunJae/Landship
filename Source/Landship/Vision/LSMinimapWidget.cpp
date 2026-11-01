#include "Vision/LSMinimapWidget.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionGeometry.h"
#include "Vision/LSVisionSubsystem.h"
#include "Vision/LSVisionPresentation.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"

FLSBattlefieldHUDLayout FLSBattlefieldHUDLayout::Calculate(FVector2D Viewport, bool bMinimap,
														   float MinimapSide)
{
	FLSBattlefieldHUDLayout Result;
	if (!bMinimap)
	{
		Result.Minimap = FBox2D(ForceInit);
		Result.Info = FBox2D(FVector2D(0, FMath::Max(0.0, Viewport.Y - 125)),
							 FVector2D(FMath::Min(340.0, Viewport.X), Viewport.Y));
		Result.Production =
			FBox2D(FVector2D(FMath::Max(0.0, Viewport.X - 360), FMath::Max(0.0, Viewport.Y - 180)), Viewport);
		return Result;
	}
	const float Side = bMinimap ? MinimapSide : 0.0f;
	Result.Scale = FMath::Max(
		0.0, FMath::Min(1.0, FMath::Min(Viewport.X / (Side + 700.0), Viewport.Y / FMath::Max(180.0f, Side))));
	const float S = Result.Scale;
	Result.Minimap = FBox2D(FVector2D(0, Viewport.Y - Side * S), FVector2D(Side * S, Viewport.Y));
	Result.Info = FBox2D(FVector2D(Side * S, Viewport.Y - 125 * S), FVector2D((Side + 340) * S, Viewport.Y));
	Result.Production = FBox2D(FVector2D(Viewport.X - 360 * S, Viewport.Y - 180 * S), Viewport);
	return Result;
}

int32 ULSMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
									const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
									int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
	Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bEnabled);
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	const ULSVisionSubsystem* Vision = GetWorld() ? GetWorld()->GetSubsystem<ULSVisionSubsystem>() : nullptr;
	if (!Settings || !Vision)
	{
		return Layer;
	}
	const FBox2D Rect = LSVisionGeometry::FitMapRect(FBox2D(FVector2D::ZeroVector, Geometry.GetLocalSize()),
													 Settings->GetWorldBounds());
	if (!Rect.bIsValid)
	{
		return Layer;
	}
	auto Box = [&](const FBox2D& Bounds, const FSlateBrush* Brush, FLinearColor Color,
				   ESlateDrawEffect Effect = ESlateDrawEffect::None) {
		FSlateDrawElement::MakeBox(Elements, ++Layer,
								   Geometry.ToPaintGeometry(FVector2f(Bounds.GetSize()),
															FSlateLayoutTransform(FVector2f(Bounds.Min))),
								   Brush, Effect, Color);
	};
	Box(FBox2D(FVector2D::ZeroVector, Geometry.GetLocalSize()), FCoreStyle::Get().GetBrush("WhiteBrush"),
		FLinearColor(0.02f, 0.03f, 0.04f));
	if (const ALSVisionPresentation* Presentation = ALSVisionPresentation::Find(GetWorld()))
	{
		for (UTextureRenderTarget2D* Texture :
			 {Presentation->GetTerrainTexture(), Presentation->GetVisibilityTexture()})
		{
			if (!Texture)
			{
				continue;
			}
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->SizeX, Texture->SizeY);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			// BaseColor 캡처의 알파는 0이다. 지형 RGB는 항상 표시하고 안개 알파만 별도로 합성한다.
			const ESlateDrawEffect Effect = Texture == Presentation->GetTerrainTexture()
												? ESlateDrawEffect::IgnoreTextureAlpha
												: ESlateDrawEffect::None;
			Box(Rect, &Brush, FLinearColor::White, Effect);
		}
	}
	for (const FLSMinimapMarker& Marker : Vision->GetMinimapMarkers())
	{
		FVector2D UV;
		if (!LSVisionGeometry::WorldToUV(FVector2D(Marker.Location), Settings->GetWorldBounds(), UV) ||
			UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1)
		{
			continue;
		}
		const FVector2D Position = Rect.Min + UV * Rect.GetSize();
		const float Radius = Marker.Kind == ELSMinimapMarkerKind::Unit ? 2.0f : 4.0f;
		const FLinearColor Color = Marker.bPublicPosition
									   ? FLinearColor::Yellow
									   : (Marker.bFriendly ? FLinearColor::Green : FLinearColor::Red);
		Box(FBox2D(Position - FVector2D(Radius), Position + FVector2D(Radius)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), Color);
	}
	APlayerController* Controller = GetOwningPlayer();
	if (Controller)
	{
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		TArray<FVector2D> UVPoints;
		for (FVector2D Corner :
			 {FVector2D(0, 0), FVector2D(Width, 0), FVector2D(Width, Height), FVector2D(0, Height)})
		{
			FVector Origin, Direction;
			if (!Controller->DeprojectScreenPositionToWorld(Corner.X, Corner.Y, Origin, Direction) ||
				FMath::Abs(Direction.Z) < UE_SMALL_NUMBER)
			{
				UVPoints.Reset();
				break;
			}
			const double Distance = (Settings->GetGroundHeight() - Origin.Z) / Direction.Z;
			if (Distance <= 0)
			{
				UVPoints.Reset();
				break;
			}
			FVector2D UV;
			if (!LSVisionGeometry::WorldToUV(FVector2D(Origin + Direction * Distance),
											 Settings->GetWorldBounds(), UV))
			{
				UVPoints.Reset();
				break;
			}
			UVPoints.Add(UV);
		}
		// 모서리를 각각 누르면 실제 화면 영역의 형태가 달라지므로 사각 경계로 다각형을 자른다.
		for (int32 Edge = 0; Edge < 4 && !UVPoints.IsEmpty(); ++Edge)
		{
			const int32 Axis = Edge / 2;
			const double Limit = Edge % 2;
			auto Inside = [&](FVector2D Point) { return Limit == 0 ? Point[Axis] >= 0 : Point[Axis] <= 1; };
			TArray<FVector2D> Clipped;
			FVector2D Previous = UVPoints.Last();
			for (FVector2D Current : UVPoints)
			{
				if (Inside(Current) != Inside(Previous))
				{
					const double T = (Limit - Previous[Axis]) / (Current[Axis] - Previous[Axis]);
					Clipped.Add(Previous + (Current - Previous) * T);
				}
				if (Inside(Current))
				{
					Clipped.Add(Current);
				}
				Previous = Current;
			}
			UVPoints = MoveTemp(Clipped);
		}
		if (UVPoints.Num() >= 3)
		{
			TArray<FVector2f> Points;
			for (FVector2D UV : UVPoints)
			{
				Points.Add(FVector2f(Rect.Min + UV * Rect.GetSize()));
			}
			const FVector2f FirstPoint = Points[0];
			Points.Add(FirstPoint);
			FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), Points,
										 ESlateDrawEffect::None, FLinearColor::White, true, 1.0f);
		}
	}
	return Layer;
}
bool ULSMinimapWidget::FocusCamera(const FGeometry& Geometry, FVector2D ScreenPosition, bool bClampToMap)
{
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	APlayerController* Controller = GetOwningPlayer();
	ALSRTSCameraPawn* Camera = Controller ? Cast<ALSRTSCameraPawn>(Controller->GetPawn()) : nullptr;
	if (!Settings || !Camera)
	{
		return false;
	}
	const FBox2D Rect = LSVisionGeometry::FitMapRect(FBox2D(FVector2D::ZeroVector, Geometry.GetLocalSize()),
													 Settings->GetWorldBounds());
	FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
	if (bClampToMap && Rect.bIsValid)
	{
		Local.X = FMath::Clamp(Local.X, Rect.Min.X, Rect.Max.X);
		Local.Y = FMath::Clamp(Local.Y, Rect.Min.Y, Rect.Max.Y);
	}
	FVector2D World;
	if (Rect.bIsValid && Rect.IsInsideOrOn(Local) &&
		LSVisionGeometry::UVToWorld((Local - Rect.Min) / Rect.GetSize(), Settings->GetWorldBounds(), World))
	{
		Camera->FocusLocation(FVector(World.X, World.Y, Settings->GetGroundHeight()));
		return true;
	}
	return false;
}
FReply ULSMinimapWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = FocusCamera(Geometry, Event.GetScreenSpacePosition());
		return bDragging ? FReply::Handled().CaptureMouse(TakeWidget()).PreventThrottling()
						 : FReply::Handled();
	}
	return FReply::Handled();
}
FReply ULSMinimapWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}
FReply ULSMinimapWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDragging)
	{
		FocusCamera(Geometry, Event.GetScreenSpacePosition(), true);
	}
	return FReply::Handled();
}
void ULSMinimapWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	bDragging = false;
	Super::NativeOnMouseCaptureLost(Event);
}
