#include "Match/Tests/LSBattleOutcomeInputInspection.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicProjectile.h"
#include "Castle/LSCastleProjectile.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Controls/LSRTSHUD.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Match/LSGameState.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Vision/LSMinimapWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

FString ULSBattleOutcomeInputInspection::Snapshot(APlayerController* InController)
{
	ALSPlayerController* Controller = Cast<ALSPlayerController>(InController);
	ALSRTSCameraPawn* Camera = Controller ? Cast<ALSRTSCameraPawn>(Controller->GetPawn()) : nullptr;
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	ALSGameState* State = World ? World->GetGameState<ALSGameState>() : nullptr;
	ALSRTSHUD* HUD = Controller ? Cast<ALSRTSHUD>(Controller->GetHUD()) : nullptr;
	UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	if (!Camera || !State || !HUD || !Client || !Client->Viewport) return TEXT("{}");
	const TSharedPtr<SViewport> ViewportWidget = Client->GetGameViewportWidget();
	if (!ViewportWidget) return TEXT("{}");
	int32 Width = 0, Height = 0;
	Controller->GetViewportSize(Width, Height);
	const FGeometry& Geometry = ViewportWidget->GetCachedGeometry();
	const FVector2D Origin = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
	const FVector2D End = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
	const FBox2D Result = HUD->ResultPanel();
	FVector2D MiniOrigin = FVector2D::ZeroVector, MiniEnd = FVector2D::ZeroVector;
	float MinimapSide = 0;
	for (TObjectIterator<ULSMinimapWidget> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsInViewport())
		{
			const FGeometry& Mini = It->GetCachedGeometry();
			MiniOrigin = Mini.LocalToAbsolute(FVector2D::ZeroVector);
			MiniEnd = Mini.LocalToAbsolute(Mini.GetLocalSize());
			MinimapSide = It->PanelSide;
			break;
		}
	}
	const FLSBattlefieldHUDLayout Layout = FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), MinimapSide > 0, MinimapSide);
	TArray<FString> Selection, ActorStates;
	for (AActor* Actor : Controller->GetSelectedActors()) Selection.Add(FString::Printf(TEXT("\"%s\""), *Actor->GetPathName().ReplaceCharWithEscapedChar()));
	for (TActorIterator<ALSUnit> It(World); It; ++It)
	{
		FVector2D Screen = FVector2D::ZeroVector;
		const bool bProjected = Controller->ProjectWorldLocationToScreen(It->GetActorLocation(), Screen);
		const FVector P = It->GetActorLocation();
		ActorStates.Add(FString::Printf(TEXT("{\"name\":\"%s\",\"position\":[%.3f,%.3f,%.3f],\"screen\":[%.3f,%.3f],\"projected\":%s,\"order\":%d,\"queue\":%d}"),
			*It->GetPathName().ReplaceCharWithEscapedChar(), P.X, P.Y, P.Z, Screen.X, Screen.Y, bProjected ? TEXT("true") : TEXT("false"), static_cast<int32>(It->Orders->GetOrderKind()), It->Orders->GetQueuedCount()));
	}
	int32 Projectiles = 0;
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It) ++Projectiles;
	for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) ++Projectiles;
	const FVector P = Camera->GetActorLocation();
	return FString::Printf(TEXT("{\"viewport\":[%d,%d],\"origin\":[%.3f,%.3f],\"end\":[%.3f,%.3f],\"window_scale\":%.3f,\"viewport_scale\":%.3f,\"focused\":%s,\"foreground\":%s,\"camera\":[%.3f,%.3f,%.3f],\"zoom\":%.3f,\"outcome\":%d,\"pending\":%d,\"population\":%d,\"production\":%d,\"weapon\":%d,\"projectiles\":%d,\"selected\":[%s],\"actors\":[%s],\"result\":[%.3f,%.3f,%.3f,%.3f],\"result_over_standard_panel\":%s,\"minimap\":[%.3f,%.3f,%.3f,%.3f],\"production_rect\":[%.3f,%.3f,%.3f,%.3f],\"minimap_captured\":%s}"),
		Width, Height, Origin.X, Origin.Y, End.X, End.Y, Window ? Window->GetDPIScaleFactor() : 0, UWidgetLayoutLibrary::GetViewportScale(World),
		Client->Viewport->HasFocus() ? TEXT("true") : TEXT("false"), Client->Viewport->IsForegroundWindow() ? TEXT("true") : TEXT("false"),
		P.X, P.Y, P.Z, Camera->GetZoomDistance(), static_cast<int32>(State->GetBattleOutcome()), static_cast<int32>(Controller->GetPendingInput()), State->GetCurrentPopulation(),
		static_cast<int32>(Controller->GetLastProductionResult()), static_cast<int32>(Controller->GetLastWeaponResult()), Projectiles, *FString::Join(Selection, TEXT(",")), *FString::Join(ActorStates, TEXT(",")),
		Result.Min.X, Result.Min.Y, Result.Max.X, Result.Max.Y, HUD->IsOverPanel(Result.GetCenter()) ? TEXT("true") : TEXT("false"),
		MiniOrigin.X, MiniOrigin.Y, MiniEnd.X, MiniEnd.Y, Layout.Production.Min.X, Layout.Production.Min.Y, Layout.Production.Max.X, Layout.Production.Max.Y,
		HUD->IsMinimapPointerCaptured() ? TEXT("true") : TEXT("false"));
}

