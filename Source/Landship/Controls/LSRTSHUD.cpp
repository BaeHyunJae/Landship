#include "Controls/LSRTSHUD.h"
#include "Vision/LSMinimapWidget.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/Canvas.h"
#include "Match/LSGameState.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ALSRTSHUD::ALSRTSHUD()
{
	static ConstructorHelpers::FClassFinder<ULSMinimapWidget> MinimapClass(
		TEXT("/Game/Landship/Vision/UI/WBP_BattlefieldMinimap"));
	MinimapWidgetClass = MinimapClass.Class;
}

void ALSRTSHUD::BeginPlay()
{
	Super::BeginPlay();
	if (PlayerOwner && MinimapWidgetClass && ALSBattlefieldSettings::Find(GetWorld()))
	{
		MinimapWidget = CreateWidget<ULSMinimapWidget>(PlayerOwner, MinimapWidgetClass);
		if (MinimapWidget)
		{
			int32 W = 0, H = 0;
			PlayerOwner->GetViewportSize(W, H);
			const FBox2D Rect =
				FLSBattlefieldHUDLayout::Calculate(FVector2D(W, H), true, MinimapWidget->PanelSide).Minimap;
			const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
			MinimapWidget->SetPositionInViewport(Rect.Min, true);
			MinimapWidget->SetDesiredSizeInViewport(Rect.GetSize() / DPI);
			MinimapWidget->AddToViewport();
		}
	}
}
void ALSRTSHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MinimapWidget)
	{
		MinimapWidget->RemoveFromParent();
		MinimapWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool ALSRTSHUD::IsOverMinimap(const FVector2D& Mouse) const
{
	int32 W = 0, H = 0;
	if (PlayerOwner)
	{
		PlayerOwner->GetViewportSize(W, H);
	}
	return MinimapWidget &&
		   FLSBattlefieldHUDLayout::Calculate(FVector2D(W, H), true, MinimapWidget->PanelSide)
			   .Minimap.IsInsideOrOn(Mouse);
}
bool ALSRTSHUD::IsMinimapWidget(const TSharedRef<SWidget>& Widget) const
{
	return MinimapWidget && MinimapWidget->GetCachedWidget() == Widget;
}
bool ALSRTSHUD::IsMinimapPointerCaptured() const
{
	return MinimapWidget && MinimapWidget->IsPointerCaptured();
}

FBox2D ALSRTSHUD::InfoPanel() const
{
	int32 Width = 0;
	int32 Height = 0;
	if (PlayerOwner)
	{
		PlayerOwner->GetViewportSize(Width, Height);
	}
	return FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), MinimapWidget != nullptr,
											  MinimapWidget ? MinimapWidget->PanelSide : 0)
		.Info;
}

FBox2D ALSRTSHUD::ProductionPanel() const
{
	int32 Width = 0;
	int32 Height = 0;
	if (PlayerOwner)
	{
		PlayerOwner->GetViewportSize(Width, Height);
	}
	return FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), MinimapWidget != nullptr,
											  MinimapWidget ? MinimapWidget->PanelSide : 0)
		.Production;
}

FBox2D ALSRTSHUD::ProductionButton(int32 Index) const
{
	const float Scale = MinimapWidget ? ProductionPanel().GetSize().X / 360.0f : 1.0f;
	const FVector2D Min = ProductionPanel().Min + FVector2D(12 + Index * 112, 55) * Scale;
	return FBox2D(Min, Min + FVector2D(104, 86) * Scale);
}

bool ALSRTSHUD::IsOverPanel(const FVector2D& Mouse) const
{
	return IsOverMinimap(Mouse) || InfoPanel().IsInsideOrOn(Mouse) || ProductionPanel().IsInsideOrOn(Mouse);
}

bool ALSRTSHUD::HandleClick(const FVector2D& Mouse)
{
	if (!IsOverPanel(Mouse))
	{
		return false;
	}
	if (ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner))
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (ProductionButton(Index).IsInsideOrOn(Mouse))
			{
				Controller->ProduceTroop(static_cast<ELSTroopType>(Index));
				break;
			}
		}
	}
	return true;
}
void ALSRTSHUD::DrawHUD()
{
	Super::DrawHUD();
	ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (!Controller || !Canvas)
	{
		return;
	}
	FVector MarkerLocation;
	FLinearColor MarkerColor;
	FVector2D MarkerPosition;
	if (Controller->GetOrderMarker(MarkerLocation, MarkerColor) &&
	    Controller->ProjectWorldLocationToScreen(MarkerLocation, MarkerPosition))
	{
		// 명령 피드백은 Shipping 빌드에서도 표시한다.
		DrawLine(MarkerPosition.X - 10, MarkerPosition.Y, MarkerPosition.X + 10, MarkerPosition.Y, MarkerColor, 2);
		DrawLine(MarkerPosition.X, MarkerPosition.Y - 10, MarkerPosition.X, MarkerPosition.Y + 10, MarkerColor, 2);
	}
	FVector2D Start;
	FVector2D End;
	if (Controller->GetSelectionBox(Start, End))
	{
		const FVector2D Min(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
		const FVector2D Max(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
		DrawRect(FLinearColor(0.1f, 0.8f, 0.3f, 0.12f), Min.X, Min.Y, Max.X - Min.X, Max.Y - Min.Y);
		DrawLine(Min.X, Min.Y, Max.X, Min.Y, FLinearColor::Green);
		DrawLine(Max.X, Min.Y, Max.X, Max.Y, FLinearColor::Green);
		DrawLine(Max.X, Max.Y, Min.X, Max.Y, FLinearColor::Green);
		DrawLine(Min.X, Max.Y, Min.X, Min.Y, FLinearColor::Green);
	}
	if (MinimapWidget)
	{
		int32 W, H;
		Controller->GetViewportSize(W, H);
		const FBox2D Rect =
			FLSBattlefieldHUDLayout::Calculate(FVector2D(W, H), true, MinimapWidget->PanelSide).Minimap;
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
		MinimapWidget->SetPositionInViewport(Rect.Min, true);
		MinimapWidget->SetDesiredSizeInViewport(Rect.GetSize() / DPI);
	}
	const TArray<AActor*> Selection = Controller->GetSelectedActors();
	for (AActor* Actor : Selection)
	{
		const bool bControlled = Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		float Radius;
		float HalfHeight;
		Actor->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const FVector Base = Actor->GetActorLocation() - FVector(0, 0, HalfHeight - 5);
		// 선택 표시는 Shipping에서도 필요하므로 디버그 드로잉에 의존하지 않는다.
		const float RingRadius = FMath::Max(Radius, 25.0f) + 8;
		for (int32 Segment = 0; Segment < 40; ++Segment)
		{
			const float AngleA = Segment * 2 * PI / 40;
			const float AngleB = (Segment + 1) * 2 * PI / 40;
			FVector2D A;
			FVector2D B;
			if (Controller->ProjectWorldLocationToScreen(
			        Base + FVector(FMath::Cos(AngleA), FMath::Sin(AngleA), 0) * RingRadius, A) &&
			    Controller->ProjectWorldLocationToScreen(
			        Base + FVector(FMath::Cos(AngleB), FMath::Sin(AngleB), 0) * RingRadius, B))
			{
				DrawLine(A.X, A.Y, B.X, B.Y, bControlled ? FLinearColor::Green : FLinearColor::Red, 2);
			}
		}
		FLSHealthSnapshot HealthSnapshot;
		const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>();
		FVector2D Screen;
		if (Health && Health->TryGetHealth(HealthSnapshot) && HealthSnapshot.MaxHealth > 0 &&
		    Controller->ProjectWorldLocationToScreen(Actor->GetActorLocation() + FVector(0, 0, HalfHeight + 20),
		                                             Screen))
		{
			DrawRect(FLinearColor::Black, Screen.X - 30, Screen.Y, 60, 6);
			DrawRect(bControlled ? FLinearColor::Green : FLinearColor::Red, Screen.X - 29, Screen.Y + 1,
			         58 * FMath::Clamp(HealthSnapshot.CurrentHealth / HealthSnapshot.MaxHealth, 0.0f, 1.0f), 4);
		}
	}
	for (const FBox2D& Box : {InfoPanel(), ProductionPanel()})
	{
		DrawRect(FLinearColor(0.025f, 0.04f, 0.07f, 0.92f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
	}
	const float PanelScale = MinimapWidget ? ProductionPanel().GetSize().X / 360.0f : 1.0f;
	auto DrawPanelText = [&](const FString& Text, FLinearColor Color, float X, float Y) {
		DrawText(Text, Color, X, Y, nullptr, PanelScale);
	};
	const FVector2D Info = InfoPanel().Min + FVector2D(12, 10) * PanelScale;
	DrawPanelText(FString::Printf(TEXT("Selected: %d"), Selection.Num()), FLinearColor::White, Info.X,
				  Info.Y);
	if (!Selection.IsEmpty())
	{
		AActor* First = Selection[0];
		const bool bControlled = First->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		DrawPanelText(First->GetName() + (bControlled ? TEXT(" [CONTROL]") : TEXT(" [INSPECT ONLY]")),
					  FLinearColor::White, Info.X, Info.Y + 22 * PanelScale);
		FLSHealthSnapshot HealthSnapshot;
		const ULSHealthComponent* Health = First->FindComponentByClass<ULSHealthComponent>();
		const bool bHealth = Health && Health->TryGetHealth(HealthSnapshot);
		DrawPanelText(bHealth ? FString::Printf(TEXT("HP %.0f / %.0f"), HealthSnapshot.CurrentHealth,
												HealthSnapshot.MaxHealth)
							  : TEXT("HP: -- (not connected)"),
					  FLinearColor::White, Info.X, Info.Y + 44 * PanelScale);
		if (const ULSOrderComponent* Orders = First->FindComponentByClass<ULSOrderComponent>())
		{
			DrawPanelText(FString::Printf(TEXT("Order: %s | Queued: %d"),
										  *UEnum::GetValueAsString(Orders->GetOrderKind()),
										  Orders->GetQueuedCount()),
						  FLinearColor::White, Info.X, Info.Y + 66 * PanelScale);
		}
	}
	const FVector2D Panel = ProductionPanel().Min + FVector2D(12, 10) * PanelScale;
	const ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	DrawPanelText(TEXT("Troop Production"), FLinearColor::White, Panel.X, Panel.Y);
	DrawPanelText(State ? FString::Printf(TEXT("Population %d / %d"), State->GetCurrentPopulation(),
										  State->GetPopulationLimit())
						: TEXT("Population --"),
				  FLinearColor::White, Panel.X, Panel.Y + 22 * PanelScale);
	const TCHAR* Labels[] = {TEXT("Q  Spearman"), TEXT("W  Archer"), TEXT("E  Cavalry")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const ELSTroopType Type = static_cast<ELSTroopType>(Index);
		const ELSProductionResult Status = Controller->Production->GetStatus(Type);
		const bool bReady = Status == ELSProductionResult::Ready;
		const FBox2D Button = ProductionButton(Index);
		DrawRect(bReady ? FLinearColor(0.08f, 0.28f, 0.32f) : FLinearColor(0.07f, 0.08f, 0.10f), Button.Min.X,
		         Button.Min.Y, Button.GetSize().X, Button.GetSize().Y);
		DrawPanelText(Labels[Index], bReady ? FLinearColor::White : FLinearColor::Gray,
					  Button.Min.X + 8 * PanelScale, Button.Min.Y + 10 * PanelScale);
		const float Remaining = Controller->Production->GetRemainingCooldown(Type);
		DrawPanelText(Remaining > 0 ? FString::Printf(TEXT("%.1fs"), Remaining)
									: (bReady ? TEXT("Produce") : TEXT("Unavailable")),
					  Remaining > 0 ? FLinearColor::Yellow : FLinearColor::White,
					  Button.Min.X + 8 * PanelScale, Button.Min.Y + 40 * PanelScale);
		if (Remaining > 0)
		{
			DrawRect(FLinearColor(0.2f, 0.65f, 0.85f), Button.Min.X, Button.Max.Y - 4,
			         Button.GetSize().X * FMath::Clamp(1 - Remaining / 10.0f, 0.0f, 1.0f), 4);
		}
	}
	const ELSProductionResult Status = Controller->Production->GetStatus(ELSTroopType::Spearman);
	const TCHAR* Message = TEXT("Independent cooldowns: 10s");
	if (Status == ELSProductionResult::SelectCastle)
		Message = TEXT("Select an allied castle");
	else if (Status == ELSProductionResult::PopulationFull)
		Message = TEXT("Population limit reached");
	else if (Status == ELSProductionResult::Unavailable)
		Message = TEXT("Production unavailable");
	else if (Controller->GetLastProductionResult() == ELSProductionResult::NoSpace)
		Message = TEXT("No reachable space near castle");
	DrawPanelText(Message, FLinearColor::White, Panel.X, ProductionPanel().Max.Y - 24 * PanelScale);
}
