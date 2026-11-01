#include "Vision/LSMinimapWidget.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Controls/LSPlayerController.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionGeometry.h"
#include "Vision/LSVisionSubsystem.h"
#include "Vision/LSVisionPresentation.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"

FLSBattlefieldHUDLayout FLSBattlefieldHUDLayout::Calculate(FVector2D Viewport, bool bMinimap, float MinimapSide)
{
	FLSBattlefieldHUDLayout Result;
	const float Side = bMinimap ? MinimapSide : 0.0f;
	Result.Scale = FMath::Max(
	    0.0, FMath::Min(1.0, FMath::Min(Viewport.X / (Side + 760.0), Viewport.Y / FMath::Max(336.0f, Side))));
	const float S = Result.Scale;
	Result.Minimap = FBox2D(FVector2D(0, Viewport.Y - Side * S), FVector2D(Side * S, Viewport.Y));
	Result.Info = FBox2D(FVector2D(Side * S, Viewport.Y - 125 * S), FVector2D((Side + 340) * S, Viewport.Y));
	Result.CommandCard = FBox2D(FVector2D(Viewport.X - 420 * S, Viewport.Y - 336 * S), Viewport);
	return Result;
}

int32 ULSMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
                                    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style,
                                    bool bEnabled) const
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
	               ESlateDrawEffect Effect = ESlateDrawEffect::None)
	{
		FSlateDrawElement::MakeBox(
		    Elements, ++Layer,
		    Geometry.ToPaintGeometry(FVector2f(Bounds.GetSize()), FSlateLayoutTransform(FVector2f(Bounds.Min))), Brush,
		    Effect, Color);
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
		if (!LSVisionGeometry::WorldToUV(FVector2D(Marker.Location), Settings->GetWorldBounds(), UV) || UV.X < 0 ||
		    UV.X > 1 || UV.Y < 0 || UV.Y > 1)
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
		for (FVector2D Corner : {FVector2D(0, 0), FVector2D(Width, 0), FVector2D(Width, Height), FVector2D(0, Height)})
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
			if (!LSVisionGeometry::WorldToUV(FVector2D(Origin + Direction * Distance), Settings->GetWorldBounds(), UV))
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
			FSlateDrawElement::MakeLines(Elements, ++Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			                             FLinearColor::White, true, 1.0f);
		}
	}
	return Layer;
}
bool ULSMinimapWidget::ResolveWorldPoint(const FGeometry& Geometry, FVector2D ScreenPosition, FVector& OutWorld,
                                         bool bClampToMap) const
{
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	if (!Settings)
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
		OutWorld = FVector(World.X, World.Y, Settings->GetGroundHeight());
		return true;
	}
	return false;
}
bool ULSMinimapWidget::FocusCamera(const FGeometry& Geometry, FVector2D ScreenPosition, bool bClampToMap)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	APlayerController* Controller = GetOwningPlayer();
	ALSRTSCameraPawn* Camera = Controller ? Cast<ALSRTSCameraPawn>(Controller->GetPawn()) : nullptr;
	FVector Location;
	if (!Camera || !ResolveWorldPoint(Geometry, ScreenPosition, Location, bClampToMap))
		return false;
	Camera->FocusLocation(Location);
	return true;
}
FReply ULSMinimapWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		EndBattleActions();
		return FReply::Handled();
	}
	// 미니맵이 마우스를 처리해도 RTS 단축키의 키보드 포커스는 전장에 둔다.
	auto FocusViewport = [this](FReply Reply)
	{
		if (UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			if (TSharedPtr<SViewport> Viewport = Client->GetGameViewportWidget())
				Reply.SetUserFocus(Viewport.ToSharedRef(), EFocusCause::Mouse);
		}
		return Reply;
	};
	const FKey Button = Event.GetEffectingButton();
	FVector Location;
	if (ALSPlayerController* Controller = Cast<ALSPlayerController>(GetOwningPlayer()))
	{
		if ((Button == EKeys::LeftMouseButton || Button == EKeys::RightMouseButton) &&
		    ResolveWorldPoint(Geometry, Event.GetScreenSpacePosition(), Location) &&
		    Controller->HandleMinimapInput(Location, Button == EKeys::RightMouseButton, Event.IsShiftDown()))
		{
			bDragging = false;
			return FocusViewport(FReply::Handled().ReleaseMouseCapture());
		}
	}
	if (Button == EKeys::LeftMouseButton)
	{
		bDragging = FocusCamera(Geometry, Event.GetScreenSpacePosition());
		return FocusViewport(bDragging ? FReply::Handled().CaptureMouse(TakeWidget()).PreventThrottling()
		                               : FReply::Handled());
	}
	return FReply::Handled();
}
FReply ULSMinimapWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		EndBattleActions();
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}
FReply ULSMinimapWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		EndBattleActions();
		return FReply::Handled();
	}
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

void ULSMinimapWidget::EndBattleActions()
{
	bDragging = false;
	if (!FSlateApplication::IsInitialized())
		return;
	const TSharedPtr<SWidget> Widget = GetCachedWidget();
	const TSharedPtr<FSlateUser> CursorUser = FSlateApplication::Get().GetCursorUser();
	if (Widget && CursorUser && CursorUser->DoesWidgetHaveCursorCapture(Widget))
		CursorUser->ReleaseCursorCapture();
}
