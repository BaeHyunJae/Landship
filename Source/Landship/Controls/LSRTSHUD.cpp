#include "Controls/LSRTSHUD.h"
#include "Vision/LSMinimapWidget.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Match/LSGameState.h"
#include "Engine/World.h"
#include "Castle/LSMobileCastle.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "CanvasItem.h"
#include "Misc/Paths.h"
#include "Fonts/CompositeFont.h"

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

void ALSRTSHUD::EndBattleActions()
{
	if (MinimapWidget) MinimapWidget->EndBattleActions();
}

bool ALSRTSHUD::IsOverMinimap(const FVector2D& Mouse) const
{
	int32 W = 0, H = 0;
	if (PlayerOwner)
	{
		PlayerOwner->GetViewportSize(W, H);
	}
	return MinimapWidget && FLSBattlefieldHUDLayout::Calculate(FVector2D(W, H), true, MinimapWidget->PanelSide)
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
	return IsOverMinimap(Mouse) || InfoPanel().IsInsideOrOn(Mouse) || ProductionPanel().IsInsideOrOn(Mouse) ||
	       WeaponPanel().IsInsideOrOn(Mouse);
}

bool ALSRTSHUD::HandleClick(const FVector2D& Mouse)
{
	if (IsOverResultPanel(Mouse))
		return true;
	if (!IsOverPanel(Mouse))
	{
		return false;
	}
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return true;
	if (ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner))
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (WeaponButton(Index).IsInsideOrOn(Mouse))
			{
				Controller->ArmCastleWeapon(static_cast<ELSCastleWeapon>(Index));
				return true;
			}
		}
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
		const FBox2D Rect = FLSBattlefieldHUDLayout::Calculate(FVector2D(W, H), true, MinimapWidget->PanelSide).Minimap;
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
		DrawSelectionRing(Actor, bControlled ? FLinearColor::Green : FLinearColor::Red);
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
	if (AActor* Target = Controller->GetAttackCommandMarker(); Target && !Selection.Contains(Target))
		DrawSelectionRing(Target, FLinearColor::Red);
	ALSMobileCastle* ArmedCastle = Controller->GetSelectedArmedCastle();
	if (ArmedCastle && Controller->GetPendingInput() == ELSCommandInput::CastleWeapon)
	{
		FVector Aim = ArmedCastle->GetActorLocation();
		const bool bValidAim =
		    Controller->GetCastleWeaponAim(Aim) &&
		    ArmedCastle->Weapons->ValidateAim(Controller->GetPendingWeapon(), Aim) == ELSWeaponCastResult::Ready;
		DrawWeaponShape(ArmedCastle, Controller->GetPendingWeapon(), Aim,
		                bValidAim ? FLinearColor(0, 0.85f, 1) : FLinearColor::Red, true);
	}
	for (TActorIterator<ALSMobileCastle> It(GetWorld()); It; ++It)
	{
		ELSCastleWeapon Weapon;
		FVector Aim;
		FLSCastleWeaponSettings Settings;
		if (It->Weapons->GetRecentCast(Weapon, Aim, &Settings))
			DrawWeaponShape(*It, Weapon, Aim, FLinearColor(1, 0.6f, 0.1f), false, &Settings);
	}
	for (const FBox2D& Box : {InfoPanel(), ProductionPanel(), WeaponPanel()})
	{
		DrawRect(FLinearColor(0.025f, 0.04f, 0.07f, 0.92f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
	}
	const float PanelScale = MinimapWidget ? ProductionPanel().GetSize().X / 360.0f : 1.0f;
	auto DrawPanelText = [&](const FString& Text, FLinearColor Color, float X, float Y)
	{ DrawText(Text, Color, X, Y, nullptr, PanelScale); };
	const FVector2D Info = InfoPanel().Min + FVector2D(12, 10) * PanelScale;
	DrawPanelText(FString::Printf(TEXT("Selected: %d"), Selection.Num()), FLinearColor::White, Info.X, Info.Y);
	if (!Selection.IsEmpty())
	{
		AActor* First = Selection[0];
		const bool bControlled = First->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		DrawPanelText(First->GetName() + (bControlled ? TEXT(" [CONTROL]") : TEXT(" [INSPECT ONLY]")),
		              FLinearColor::White, Info.X, Info.Y + 22 * PanelScale);
		FLSHealthSnapshot HealthSnapshot;
		const ULSHealthComponent* Health = First->FindComponentByClass<ULSHealthComponent>();
		const bool bHealth = Health && Health->TryGetHealth(HealthSnapshot);
		DrawPanelText(
		    bHealth ? FString::Printf(TEXT("HP %.0f / %.0f"), HealthSnapshot.CurrentHealth, HealthSnapshot.MaxHealth)
		            : TEXT("HP: -- (not connected)"),
		    FLinearColor::White, Info.X, Info.Y + 44 * PanelScale);
		if (const ULSOrderComponent* Orders = First->FindComponentByClass<ULSOrderComponent>())
		{
			DrawPanelText(FString::Printf(TEXT("Order: %s | Queued: %d"),
			                              *UEnum::GetValueAsString(Orders->GetOrderKind()), Orders->GetQueuedCount()),
			              FLinearColor::White, Info.X, Info.Y + 66 * PanelScale);
			if (Orders->GetLastFailure() != ELSOrderFailure::None)
			{
				const TCHAR* Failure =
				    Orders->GetLastFailure() == ELSOrderFailure::QueueFull
				        ? TEXT("Queue full")
				        : (Orders->GetLastFailure() == ELSOrderFailure::InvalidTarget
				               ? TEXT("Invalid target")
				               : (Orders->GetLastFailure() == ELSOrderFailure::Unreachable ? TEXT("No reachable path")
				                                                                           : TEXT("Unavailable")));
				DrawPanelText(FString(TEXT("Last command: ")) + Failure, FLinearColor::Yellow, Info.X,
				              Info.Y + 88 * PanelScale);
			}
		}
	}
	const FVector2D Panel = ProductionPanel().Min + FVector2D(12, 10) * PanelScale;
	const ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	DrawPanelText(TEXT("Troop Production"), FLinearColor::White, Panel.X, Panel.Y);
	DrawPanelText(
	    State ? FString::Printf(TEXT("Population %d / %d"), State->GetCurrentPopulation(), State->GetPopulationLimit())
	          : TEXT("Population --"),
	    FLinearColor::White, Panel.X, Panel.Y + 22 * PanelScale);
	const TCHAR* Labels[] = {TEXT("F1 Spearman"), TEXT("F2 Archer"), TEXT("F3 Cavalry")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const ELSTroopType Type = static_cast<ELSTroopType>(Index);
		const ELSProductionResult Status = Controller->Production->GetStatus(Type);
		const bool bReady = Status == ELSProductionResult::Ready;
		const FBox2D Button = ProductionButton(Index);
		DrawRect(bReady ? FLinearColor(0.08f, 0.28f, 0.32f) : FLinearColor(0.07f, 0.08f, 0.10f), Button.Min.X,
		         Button.Min.Y, Button.GetSize().X, Button.GetSize().Y);
		DrawPanelText(Labels[Index], bReady ? FLinearColor::White : FLinearColor::Gray, Button.Min.X + 8 * PanelScale,
		              Button.Min.Y + 10 * PanelScale);
		const float Remaining = Controller->Production->GetRemainingCooldown(Type);
		DrawPanelText(Remaining > 0 ? FString::Printf(TEXT("%.1fs"), Remaining)
		                            : (bReady ? TEXT("Produce") : TEXT("Unavailable")),
		              Remaining > 0 ? FLinearColor::Yellow : FLinearColor::White, Button.Min.X + 8 * PanelScale,
		              Button.Min.Y + 40 * PanelScale);
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
	const bool bBattleEnded = LSBattleOutcome::AreActionsStopped(GetWorld());
	const FVector2D Weapons = WeaponPanel().Min + FVector2D(12, 10) * PanelScale;
	DrawPanelText(TEXT("Castle Weapons"), FLinearColor::White, Weapons.X, Weapons.Y);
	const TCHAR* WeaponLabels[] = {TEXT("Q  Cannon"), TEXT("W  Shotgun"), TEXT("E  Ballista")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const ELSCastleWeapon Weapon = static_cast<ELSCastleWeapon>(Index);
		const bool bReady = ArmedCastle && ArmedCastle->Weapons->GetStatus(Weapon) == ELSWeaponCastResult::Ready;
		const bool bAiming =
		    Controller->GetPendingInput() == ELSCommandInput::CastleWeapon && Controller->GetPendingWeapon() == Weapon;
		const FBox2D Button = WeaponButton(Index);
		DrawRect(bAiming ? FLinearColor(0.4f, 0.22f, 0.05f)
		                 : (bReady ? FLinearColor(0.1f, 0.25f, 0.35f) : FLinearColor(0.07f, 0.08f, 0.1f)),
		         Button.Min.X, Button.Min.Y, Button.GetSize().X, Button.GetSize().Y);
		DrawPanelText(WeaponLabels[Index], bReady ? FLinearColor::White : FLinearColor::Gray,
		              Button.Min.X + 8 * PanelScale, Button.Min.Y + 10 * PanelScale);
		const float Remaining = !bBattleEnded && ArmedCastle ? ArmedCastle->Weapons->GetRemainingCooldown(Weapon) : 0;
		FLSCastleWeaponState WeaponState;
		const bool bHasState = ArmedCastle && ArmedCastle->Weapons->GetWeaponState(Weapon, WeaponState);
		if (bHasState)
			DrawPanelText(
			    FString::Printf(TEXT("Lv %d / %d"), WeaponState.Level, ArmedCastle->Weapons->GetMaxLevel(Weapon)),
			    FLinearColor::White, Button.Min.X + 8 * PanelScale, Button.Min.Y + 27 * PanelScale);
		const TCHAR* Availability = bReady ? TEXT("Ready") : TEXT("Unavailable");
		if (!bBattleEnded && bHasState && !WeaponState.bUnlocked)
			Availability = TEXT("Locked");
		else if (!bBattleEnded && bHasState && !WeaponState.bEnabled)
			Availability = TEXT("Disabled");
		DrawPanelText(Remaining > 0 && bHasState && WeaponState.bUnlocked && WeaponState.bEnabled
		                  ? FString::Printf(TEXT("%.1fs"), Remaining)
		                  : (bAiming ? TEXT("Aiming") : Availability),
		              Remaining > 0 ? FLinearColor::Yellow : FLinearColor::White, Button.Min.X + 8 * PanelScale,
		              Button.Min.Y + 40 * PanelScale);
		if (Remaining > 0 && bHasState && (!WeaponState.bUnlocked || !WeaponState.bEnabled))
			DrawPanelText(FString::Printf(TEXT("CD %.1fs"), Remaining), FLinearColor::Yellow,
			              Button.Min.X + 8 * PanelScale, Button.Min.Y + 60 * PanelScale);
	}
	const TCHAR* WeaponMessage = bBattleEnded ? TEXT("Battle ended")
		: (ArmedCastle ? TEXT("Select weapon, then left click") : TEXT("Select an allied castle"));
	if (!bBattleEnded && Controller->GetPendingInput() == ELSCommandInput::CastleWeapon)
		WeaponMessage = Controller->GetLastWeaponResult() == ELSWeaponCastResult::OutOfRange
		                    ? TEXT("Out of range | RMB/Esc: cancel")
		                    : TEXT("Left click: fire | RMB/Esc: cancel");
	DrawPanelText(WeaponMessage, FLinearColor::White, Weapons.X, WeaponPanel().Max.Y - 24 * PanelScale);
	DrawBattleResult();
}

FBox2D ALSRTSHUD::ResultPanel() const
{
	int32 Width = 0, Height = 0;
	if (PlayerOwner)
		PlayerOwner->GetViewportSize(Width, Height);
	float AvailableHeight = FMath::Max(0.0, FMath::Min(InfoPanel().Min.Y, WeaponPanel().Min.Y));
	if (MinimapWidget)
		AvailableHeight = FMath::Min(AvailableHeight,
		    static_cast<float>(FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), true, MinimapWidget->PanelSide)
		                           .Minimap.Min.Y));
	const FVector2D Size(FMath::Min(520.0f, Width * 0.8f), FMath::Min(180.0f, AvailableHeight * 0.6f));
	const FVector2D Center(Width * 0.5f, AvailableHeight * 0.5f);
	return FBox2D(Center - Size * 0.5f, Center + Size * 0.5f);
}

bool ALSRTSHUD::IsOverResultPanel(const FVector2D& Mouse) const
{
	return LSBattleOutcome::AreActionsStopped(GetWorld()) && ResultPanel().IsInsideOrOn(Mouse);
}

void ALSRTSHUD::DrawBattleResult()
{
	const ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	if (!State || !State->IsBattleOutcomeEnabled() || !State->HasBattleEnded())
		return;
	DrawRect(FLinearColor(0, 0, 0, 0.5f), 0, 0, Canvas->SizeX, Canvas->SizeY);
	const FBox2D Panel = ResultPanel();
	DrawRect(FLinearColor(0.025f, 0.04f, 0.07f, 0.96f), Panel.Min.X, Panel.Min.Y,
	         Panel.GetSize().X, Panel.GetSize().Y);
	const bool bVictory = LSBattleOutcome::IsVictory(State->GetBattleOutcome());
	// 기본 Canvas 폰트에 의존하지 않고 프로토타입은 에디터에 포함된 한글 폰트를 사용한다.
	if (!ResultFont)
	{
		// Canvas는 Slate 정보뿐 아니라 실제 UFont가 있어야 텍스트를 그린다.
		ResultFont = NewObject<UFont>(this);
		ResultFont->FontCacheType = EFontCacheType::Runtime;
		ResultFont->GetMutableInternalCompositeFont() = FCompositeFont(
			NAME_None, FPaths::EngineContentDir() / TEXT("Editor/Slate/Fonts/NanumGothic.ttf"),
			EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}
	const float Scale = FMath::Min(Panel.GetSize().X / 520.0f, Panel.GetSize().Y / 180.0f);
	auto DrawResultText = [&](const TCHAR* Text, float Size, float Y, FLinearColor Color)
	{
		FCanvasTextItem Item(FVector2D(Panel.GetCenter().X, Y), FText::FromString(Text),
		                     FSlateFontInfo(ResultFont.Get(), Size * Scale), Color);
		Item.bCentreX = true;
		Canvas->DrawItem(Item);
	};
	DrawResultText(bVictory ? TEXT("승리") : TEXT("패배"), 36, Panel.Min.Y + 34 * Scale,
	               bVictory ? FLinearColor(0.4f, 1, 0.6f) : FLinearColor(1, 0.35f, 0.3f));
	DrawResultText(bVictory ? TEXT("핵심 거점 함락") : TEXT("성 파괴"), 20,
	               Panel.Min.Y + 108 * Scale, FLinearColor::White);
}

FBox2D ALSRTSHUD::WeaponPanel() const
{
	const FBox2D Production = ProductionPanel();
	return FBox2D(Production.Min - FVector2D(0, 156) * (Production.GetSize().X / 360.0f),
	              FVector2D(Production.Max.X, Production.Min.Y));
}

FBox2D ALSRTSHUD::WeaponButton(int32 Index) const
{
	const float Scale = ProductionPanel().GetSize().X / 360.0f;
	const FVector2D Min = WeaponPanel().Min + FVector2D(12 + Index * 112, 40) * Scale;
	return FBox2D(Min, Min + FVector2D(104, 82) * Scale);
}

void ALSRTSHUD::DrawWorldLine(const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness)
{
	FVector2D A, B;
	if (PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(Start, A) &&
	    PlayerOwner->ProjectWorldLocationToScreen(End, B))
		DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
}

void ALSRTSHUD::DrawWeaponShape(ALSMobileCastle* Castle, ELSCastleWeapon Weapon, const FVector& Aim,
                                const FLinearColor& Color, bool bPreview, const FLSCastleWeaponSettings* Snapshot)
{
	const FLSCastleWeaponSettings* Settings = Snapshot ? Snapshot : Castle->Weapons->FindSettings(Weapon);
	if (!Settings)
		return;
	float Radius, HalfHeight;
	Castle->GetSimpleCollisionCylinder(Radius, HalfHeight);
	const FVector Origin = Castle->GetActorLocation() - FVector(0, 0, HalfHeight - 6);
	const auto DrawCircle = [this](const FVector& Center, float CircleRadius, const FLinearColor& CircleColor)
	{
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const float A = Index * 2 * PI / 64;
			const float B = (Index + 1) * 2 * PI / 64;
			DrawWorldLine(Center + FVector(FMath::Cos(A), FMath::Sin(A), 0) * CircleRadius,
			              Center + FVector(FMath::Cos(B), FMath::Sin(B), 0) * CircleRadius, CircleColor);
		}
	};
	if (Weapon == ELSCastleWeapon::Cannon)
	{
		if (bPreview)
			DrawCircle(Origin, Settings->Range, FLinearColor(0.15f, 0.4f, 0.7f));
		DrawCircle(Aim + FVector(0, 0, 6), Settings->Radius, Color);
		return;
	}
	const FVector Direction = (Aim - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		return;
	if (Weapon == ELSCastleWeapon::Shotgun)
	{
		const float Angle = FMath::Atan2(Direction.Y, Direction.X);
		const float HalfAngle = FMath::DegreesToRadians(Settings->ConeAngle / 2);
		FVector Previous = Origin;
		for (int32 Index = 0; Index <= 40; ++Index)
		{
			const float CurrentAngle = Angle - HalfAngle + HalfAngle * 2 * Index / 40;
			const FVector Point =
			    Origin + FVector(FMath::Cos(CurrentAngle), FMath::Sin(CurrentAngle), 0) * Settings->Range;
			DrawWorldLine(Previous, Point, Color);
			Previous = Point;
		}
		DrawWorldLine(Previous, Origin, Color);
	}
	else
	{
		const FVector Side(-Direction.Y * Settings->Radius, Direction.X * Settings->Radius, 0);
		const FVector End = Origin + Direction * Settings->Range;
		DrawWorldLine(Origin + Side, End + Side, Color);
		DrawWorldLine(Origin - Side, End - Side, Color);
		DrawWorldLine(End + Side, End - Side, Color);
		DrawWorldLine(End, End - Direction * 80 + FVector(-Direction.Y, Direction.X, 0) * 45, Color);
		DrawWorldLine(End, End - Direction * 80 - FVector(-Direction.Y, Direction.X, 0) * 45, Color);
	}
}

bool ALSRTSHUD::GetMinimapWorldPoint(FVector& OutLocation) const
{
	return MinimapWidget && FSlateApplication::IsInitialized() &&
	       MinimapWidget->ResolveWorldPoint(MinimapWidget->GetCachedGeometry(), FSlateApplication::Get().GetCursorPos(),
	                                        OutLocation);
}

void ALSRTSHUD::DrawSelectionRing(AActor* Actor, const FLinearColor& Color)
{
	float Radius, HalfHeight;
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
		if (PlayerOwner->ProjectWorldLocationToScreen(
		        Base + FVector(FMath::Cos(AngleA), FMath::Sin(AngleA), 0) * RingRadius, A) &&
		    PlayerOwner->ProjectWorldLocationToScreen(
		        Base + FVector(FMath::Cos(AngleB), FMath::Sin(AngleB), 0) * RingRadius, B))
		{
			DrawLine(A.X, A.Y, B.X, B.Y, Color, 2);
		}
	}
}
