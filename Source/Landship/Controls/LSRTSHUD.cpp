#include "Controls/LSRTSHUD.h"
#include "Controls/LSCommandCardComponent.h"
#include "HUD/LSBattleHUDWidget.h"
#include "Vision/LSMinimapWidget.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSFactionInterface.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Vision/LSVisionSubsystem.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Obstacles/LSBarricade.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Match/LSGameState.h"
#include "Engine/World.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleProjectile.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "CanvasItem.h"
#include "Misc/Paths.h"
#include "Fonts/CompositeFont.h"

ALSRTSHUD::ALSRTSHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	static ConstructorHelpers::FClassFinder<ULSMinimapWidget> MinimapClass(
	    TEXT("/Game/Landship/Vision/UI/WBP_BattlefieldMinimap"));
	MinimapWidgetClass = MinimapClass.Class;
	BattleWidgetClass = TSoftClassPtr<ULSBattleHUDWidget>(FSoftObjectPath(TEXT("/Game/Landship/HUD/UI/WBP_BattleHUD.WBP_BattleHUD_C")));
}

void ALSRTSHUD::BeginPlay()
{
	Super::BeginPlay();
	bCombatFeedbackActive = true;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) BindCombatActor(*It);
	ActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ALSRTSHUD::BindCombatActor));
	if (PlayerOwner)
	{
		if (auto* Class = BattleWidgetClass.LoadSynchronous())
		{
			BattleWidget = CreateWidget<ULSBattleHUDWidget>(PlayerOwner, Class);
			if (BattleWidget)
			{
				BattleWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				BattleWidget->AddToViewport(1);
			}
		}
	}
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
	bCombatFeedbackActive = false;
	PendingDamage.Reset();
	PendingAttacks.Reset();
	DamageFeedback.Reset();
	AttackFeedback.Reset();
	DamagedActors.Reset();
	GetWorld()->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	for (const auto& Health : BoundHealth)
		if (Health.IsValid()) Health->OnDamageCommitted.RemoveAll(this);
	for (const auto& Attack : BoundAttacks)
		if (Attack.IsValid()) Attack->OnAttackExecutedEvent.RemoveDynamic(this, &ALSRTSHUD::OnAttackExecuted);
	for (const auto& Weapons : BoundWeapons)
		if (Weapons.IsValid()) Weapons->OnAttackExecutedEvent.RemoveDynamic(this, &ALSRTSHUD::OnAttackExecuted);
	for (int32 Index = HitOverlays.Num() - 1; Index >= 0; --Index) RestoreOverlay(Index);
	if (BattleWidget)
	{
		BattleWidget->RemoveFromParent();
		BattleWidget = nullptr;
	}
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
bool ALSRTSHUD::IsBattleWidget(const TSharedRef<SWidget>& Widget) const
{
	return BattleWidget && BattleWidget->GetCachedWidget() == Widget;
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

FBox2D ALSRTSHUD::CommandPanel() const
{
	int32 Width = 0;
	int32 Height = 0;
	if (PlayerOwner)
	{
		PlayerOwner->GetViewportSize(Width, Height);
	}
	return FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), MinimapWidget != nullptr,
	                                          MinimapWidget ? MinimapWidget->PanelSide : 0)
	    .CommandCard;
}

FBox2D ALSRTSHUD::GetCommandCellRect(int32 Index) const
{
	if (Index < 0 || Index >= ULSCommandCardComponent::CellCount) return FBox2D(ForceInit);
	const float Scale = CommandPanel().GetSize().X / 420.0f;
	const FVector2D Min = CommandPanel().Min + FVector2D(12 + Index % 5 * 80, 36 + Index / 5 * 82) * Scale;
	return FBox2D(Min, Min + FVector2D(76, 78) * Scale);
}

bool ALSRTSHUD::IsOverPanel(const FVector2D& Mouse) const
{
	const FBox2D CastleStatus = GetCastleStatusRect();
	return IsOverMinimap(Mouse) || InfoPanel().IsInsideOrOn(Mouse) || CommandPanel().IsInsideOrOn(Mouse) ||
	       (CastleStatus.bIsValid && CastleStatus.IsInsideOrOn(Mouse));
}

FBox2D ALSRTSHUD::GetCastleStatusRect() const
{
	const auto* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (!Controller || !Controller->GetConfiguredBattlefieldCastle())
		return FBox2D(ForceInit);
	int32 Width = 0, Height = 0;
	if (PlayerOwner) PlayerOwner->GetViewportSize(Width, Height);
	const float Scale = FLSBattlefieldHUDLayout::Calculate(FVector2D(Width, Height), MinimapWidget != nullptr,
	    MinimapWidget ? MinimapWidget->PanelSide : 0).Scale;
	const FVector2D Min = FVector2D(20, 20) * Scale;
	return FBox2D(Min, Min + FVector2D(420, 154) * Scale);
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
		for (int32 Index = 0; Index < ULSCommandCardComponent::CellCount; ++Index)
			if (GetCommandCellRect(Index).IsInsideOrOn(Mouse))
			{
				if (BattleWidget) BattleWidget->ExecuteCommandCell(Index);
				break;
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
	DrawCombatFeedback(Selection);
	ALSCastle* ArmedCastle = Controller->GetAimingCastle();
	if (ArmedCastle && Controller->GetPendingInput() == ELSCommandInput::CastleWeapon)
	{
		FVector Aim = ArmedCastle->GetActorLocation();
		const bool bValidAim =
		    Controller->GetCastleWeaponAim(Aim) &&
		    ArmedCastle->Weapons->ValidateAim(Controller->GetPendingWeapon(), Aim) == ELSWeaponCastResult::Ready;
		DrawWeaponShape(ArmedCastle, Controller->GetPendingWeapon(), Aim,
		                bValidAim ? FLinearColor(0, 0.85f, 1) : FLinearColor::Red, true);
	}
	for (TActorIterator<ALSCastle> It(GetWorld()); It; ++It)
	{
		ELSCastleWeapon Weapon;
		FVector Aim;
		FLSCastleWeaponSettings Settings;
		if (It->Weapons->GetRecentCast(Weapon, Aim, &Settings))
			DrawWeaponShape(*It, Weapon, Aim, FLinearColor(1, 0.6f, 0.1f), false, &Settings);
	}
	if (BattleWidget)
	{
		BattleWidget->SetPanelLayout(InfoPanel(), CommandPanel());
		BattleWidget->SetCastleStatusLayout(GetCastleStatusRect());
	}
	DrawBattleResult();
}

FBox2D ALSRTSHUD::ResultPanel() const
{
	int32 Width = 0, Height = 0;
	if (PlayerOwner)
		PlayerOwner->GetViewportSize(Width, Height);
	float AvailableHeight = FMath::Max(0.0, FMath::Min(InfoPanel().Min.Y, CommandPanel().Min.Y));
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

void ALSRTSHUD::DrawWorldLine(const FVector& Start, const FVector& End, const FLinearColor& Color, float Thickness)
{
	FVector2D A, B;
	if (PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(Start, A) &&
	    PlayerOwner->ProjectWorldLocationToScreen(End, B))
		DrawLine(A.X, A.Y, B.X, B.Y, Color, Thickness);
}

void ALSRTSHUD::DrawWeaponShape(ALSCastle* Castle, ELSCastleWeapon Weapon, const FVector& Aim,
                                const FLinearColor& Color, bool bPreview, const FLSCastleWeaponSettings* Snapshot)
{
	const FLSCastleWeaponSettings* Settings = Snapshot ? Snapshot : Castle->Weapons->FindSettings(Weapon);
	if (!Settings)
		return;
	float Radius, HalfHeight;
	Castle->GetSimpleCollisionCylinder(Radius, HalfHeight);
	FVector Origin = Castle->GetActorLocation() - FVector(0, 0, HalfHeight - 6);
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
	const FVector Direction = (Aim - Castle->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		return;
	if (Weapon == ELSCastleWeapon::Shotgun)
	{
		Origin = Castle->Weapons->GetWeaponFireOrigin(Weapon, Aim);
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
			DrawLine(A.X, A.Y, B.X, B.Y, Color, 3);
		}
	}
}

bool ALSRTSHUD::UsesUnifiedCombatFeedback(UWorld* World)
{
	if (!World) return false;
	for (TActorIterator<ALSRTSHUD> It(World); It; ++It)
		if (IsValid(*It) && It->bCombatFeedbackActive && !It->IsActorBeingDestroyed()) return true;
	return false;
}

bool ALSRTSHUD::IsFeedbackVisible(const AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->IsHidden()) return false;
	const auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	return !Vision || Vision->IsActorVisibleToFriendly(Actor);
}

bool ALSRTSHUD::IsFeedbackPointVisible(const FVector& Point) const
{
	const auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	return !Vision || !Vision->IsVisionEnabled() || Vision->IsPointVisible(Point);
}

FBox ALSRTSHUD::GetVisualBodyBounds(AActor* Actor) const
{
	FBox Bounds(ForceInit);
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
		if (Mesh->IsVisible() && !Mesh->bHiddenInGame && Mesh->GetName() != TEXT("SelectionRing"))
			Bounds += Mesh->Bounds.GetBox();
	if (!Bounds.IsValid) Bounds = Actor->GetComponentsBoundingBox(true);
	return Bounds;
}

void ALSRTSHUD::BindCombatActor(AActor* Actor)
{
	if (!bCombatFeedbackActive || !Actor || Actor == this) return;
	if (auto* Health = Actor->FindComponentByClass<ULSHealthComponent>())
	{
		if (!BoundHealth.Contains(TWeakObjectPtr<ULSHealthComponent>(Health)))
		{
			BoundHealth.Add(Health);
			Health->OnDamageCommitted.AddUObject(this, &ALSRTSHUD::OnDamageCommitted);
			FLSDamageResult Last;
			if (Health->HasTakenDamage() && Health->TryGetLastDamage(Last))
			{
				FLSCombatHealthDisplay& Display = DamagedActors.FindOrAdd(Actor);
				Display.BeforeFraction = Display.AfterFraction = Last.MaxHealth > 0 ? Last.CurrentHealth / Last.MaxHealth : 0;
			}
		}
	}
	if (auto* Attack = Actor->FindComponentByClass<ULSBasicAttackComponent>(); Attack && !BoundAttacks.Contains(TWeakObjectPtr<ULSBasicAttackComponent>(Attack)))
	{
		BoundAttacks.Add(Attack);
		Attack->OnAttackExecutedEvent.AddDynamic(this, &ALSRTSHUD::OnAttackExecuted);
	}
	if (auto* Weapons = Actor->FindComponentByClass<ULSCastleWeaponComponent>(); Weapons && !BoundWeapons.Contains(TWeakObjectPtr<ULSCastleWeaponComponent>(Weapons)))
	{
		BoundWeapons.Add(Weapons);
		Weapons->OnAttackExecutedEvent.AddDynamic(this, &ALSRTSHUD::OnAttackExecuted);
	}
	if (auto* Projectile = Cast<ALSBasicProjectile>(Actor)) Projectiles.AddUnique(Projectile);
	if (auto* Projectile = Cast<ALSCastleProjectile>(Actor)) CastleProjectiles.AddUnique(Projectile);
}

void ALSRTSHUD::OnDamageCommitted(const FLSDamageResult& Damage)
{
	if (bCombatFeedbackActive) PendingDamage.Add(Damage);
}

void ALSRTSHUD::OnAttackExecuted(const FLSAttackExecution& Execution)
{
	if (bCombatFeedbackActive && IsFeedbackVisible(Execution.Source.Get()) && IsFeedbackVisible(Execution.Target.Get()) &&
	    IsFeedbackPointVisible(Execution.SourceLocation) && IsFeedbackPointVisible(Execution.TargetLocation))
		PendingAttacks.Add(Execution);
}

void ALSRTSHUD::RestoreOverlay(int32 Index)
{
	const FLSHitOverlay& Overlay = HitOverlays[Index];
	if (Overlay.Mesh.IsValid() && Overlay.Mesh->GetOverlayMaterial() == HitMaterial)
		Overlay.Mesh->SetOverlayMaterial(Overlay.Previous);
	HitOverlays.RemoveAtSwap(Index);
}

void ALSRTSHUD::FlashHit(AActor* Actor, double ExpiresAt)
{
	if (!HitMaterial)
	{
		auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Landship/Troops/Materials/M_TroopPrototype.M_TroopPrototype"));
		if (!Material) return;
		HitMaterial = UMaterialInstanceDynamic::Create(Material, this);
		HitMaterial->SetVectorParameterValue(TEXT("TeamColor"), FLinearColor::White);
	}
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->IsVisible() || Mesh->bHiddenInGame) continue;
		FLSHitOverlay* Existing = HitOverlays.FindByPredicate([Mesh](const FLSHitOverlay& Value) { return Value.Mesh == Mesh; });
		if (Existing) { Existing->ExpiresAt = ExpiresAt; Existing->StartedFrame = GFrameCounter; continue; }
		FLSHitOverlay& Overlay = HitOverlays.AddDefaulted_GetRef();
		Overlay.Mesh = Mesh;
		Overlay.Actor = Actor;
		Overlay.Previous = Mesh->GetOverlayMaterial();
		Overlay.ExpiresAt = ExpiresAt;
		Overlay.StartedFrame = GFrameCounter;
		Mesh->SetOverlayMaterial(HitMaterial);
	}
}

void ALSRTSHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bCombatFeedbackActive) return;
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FLSDamageResult& Damage : PendingDamage)
	{
		AActor* Actor = Damage.Receiver.Get();
		if (Damage.AppliedDamage <= 0 || !IsValid(Actor)) continue;
		const auto* Health = Actor->FindComponentByClass<ULSHealthComponent>();
		FLSHealthSnapshot LiveHealth;
		if (!Health || !Health->TryGetHealth(LiveHealth) || LiveHealth.MaxHealth <= 0) continue;
		FLSCombatHealthDisplay& Display = DamagedActors.FindOrAdd(Actor);
		const float PreviousGhost = Display.GetGhostFraction(Now);
		Display.AfterFraction = FMath::Clamp(LiveHealth.CurrentHealth / LiveHealth.MaxHealth, 0.0f, 1.0f);
		const bool bPresentHit = !Damage.bReceiverHiddenAtCommit && IsFeedbackVisible(Actor);
		Display.BeforeFraction = bPresentHit
			? FMath::Max(PreviousGhost, Damage.MaxHealth > 0 ? Damage.PreviousHealth / Damage.MaxHealth : 0)
			: Display.AfterFraction;
		Display.PresentationTime = bPresentHit ? Now : -1;
		if (bPresentHit)
		{
			FlashHit(Actor, Now + 0.1);
			DamageFeedback.Add({Damage, Now});
		}
	}
	PendingDamage.Reset();
	for (const FLSAttackExecution& Attack : PendingAttacks)
		if (!Attack.bProjectile && IsFeedbackVisible(Attack.Source.Get()) && IsFeedbackVisible(Attack.Target.Get()) &&
		    IsFeedbackPointVisible(Attack.SourceLocation) && IsFeedbackPointVisible(Attack.TargetLocation))
			AttackFeedback.Add({Attack, Now, GFrameCounter});
	PendingAttacks.Reset();
	for (int32 Index = HitOverlays.Num() - 1; Index >= 0; --Index)
		if ((Now >= HitOverlays[Index].ExpiresAt && GFrameCounter > HitOverlays[Index].StartedFrame) ||
		    !IsFeedbackVisible(HitOverlays[Index].Actor.Get())) RestoreOverlay(Index);
	AttackFeedback.RemoveAll([this, Now](const FLSCombatAttackPresentation& Presentation) {
		return (Now >= Presentation.ReceivedAt + 0.12 && GFrameCounter > Presentation.StartedFrame) ||
			!IsFeedbackVisible(Presentation.Execution.Source.Get()) || !IsFeedbackVisible(Presentation.Execution.Target.Get()); });
	DamageFeedback.RemoveAll([this, Now](const FLSCombatDamagePresentation& Presentation) {
		return Now >= Presentation.ReceivedAt + 0.3 || !IsFeedbackVisible(Presentation.Damage.Receiver.Get()); });
	for (auto It = DamagedActors.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !ULSCombatTargetLibrary::IsCombatAlive(It.Key().Get())) It.RemoveCurrent();
	Projectiles.RemoveAll([](const auto& Value) { return !Value.IsValid(); });
	CastleProjectiles.RemoveAll([](const auto& Value) { return !Value.IsValid(); });
	BoundWeapons.RemoveAll([](const auto& Value) { return !Value.IsValid(); });
	BoundHealth.RemoveAll([](const auto& Value) { return !Value.IsValid(); });
	BoundAttacks.RemoveAll([](const auto& Value) { return !Value.IsValid(); });
}

void ALSRTSHUD::DrawCombatFeedback(const TArray<AActor*>& Selection)
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (TActorIterator<ALSCastle> It(GetWorld()); It; ++It)
	{
		if (!IsFeedbackVisible(*It)) continue;
		float Radius, HalfHeight;
		It->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const FVector Forward = It->GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Center = It->GetActorLocation() + FVector(0, 0, HalfHeight + 20);
		const FVector Tip = Center + Forward * Radius * 0.65f;
		DrawWorldLine(Center - Forward * Radius * 0.25f, Tip, FLinearColor::White, 3);
		DrawWorldLine(Tip, Tip - Forward * Radius * 0.25f + Right * Radius * 0.18f, FLinearColor::White, 3);
		DrawWorldLine(Tip, Tip - Forward * Radius * 0.25f - Right * Radius * 0.18f, FLinearColor::White, 3);
	}
	TSet<AActor*> Actors;
	for (AActor* Actor : Selection) Actors.Add(Actor);
	for (const auto& Pair : DamagedActors) if (Pair.Key.IsValid()) Actors.Add(Pair.Key.Get());
	for (AActor* Actor : Actors)
	{
		if (!IsFeedbackVisible(Actor) || !ULSCombatTargetLibrary::IsCombatAlive(Actor)) continue;
		const bool bSelected = Selection.Contains(Actor);
		const auto* Faction = Cast<ILSFactionInterface>(Actor);
		const FLinearColor Color = Faction && Faction->GetFaction() == ELSFaction::Ally ? FLinearColor(0.2f, 1, 0.35f) : FLinearColor(1, 0.22f, 0.18f);
		const FBox Bounds = GetVisualBodyBounds(Actor);
		if (bSelected)
		{
			DrawSelectionRing(Actor, Color);
			FBox2D ScreenBounds(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				FVector2D Point;
				const FVector World((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
					(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
				if (PlayerOwner->ProjectWorldLocationToScreen(World, Point)) ScreenBounds += Point;
			}
			if (ScreenBounds.bIsValid)
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const float X = (Corner & 1) ? ScreenBounds.Max.X : ScreenBounds.Min.X;
					const float Y = (Corner & 2) ? ScreenBounds.Max.Y : ScreenBounds.Min.Y;
					DrawLine(X, Y, X + ((Corner & 1) ? -8 : 8), Y, Color, 2);
					DrawLine(X, Y, X, Y + ((Corner & 2) ? -8 : 8), Color, 2);
				}
		}
		FLSHealthSnapshot HP;
		const auto* Health = Actor->FindComponentByClass<ULSHealthComponent>();
		FVector2D Screen;
		if (!Health || !Health->TryGetHealth(HP) || HP.MaxHealth <= 0 ||
		    !PlayerOwner->ProjectWorldLocationToScreen(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + 12), Screen)) continue;
		const float Width = bSelected ? 60 : 40, Height = bSelected ? 6 : 4;
		const float Fraction = FMath::Clamp(HP.CurrentHealth / HP.MaxHealth, 0.0f, 1.0f);
		float Ghost = Fraction;
		if (const FLSCombatHealthDisplay* Display = DamagedActors.Find(Actor))
			Ghost = FMath::Max(Fraction, Display->GetGhostFraction(Now));
		DrawRect(FLinearColor::Black, Screen.X - Width / 2, Screen.Y, Width, Height);
		DrawRect(FLinearColor(1, 0.85f, 0.7f), Screen.X - Width / 2 + 1, Screen.Y + 1, (Width - 2) * Ghost, Height - 2);
		DrawRect(Color, Screen.X - Width / 2 + 1, Screen.Y + 1, (Width - 2) * Fraction, Height - 2);
	}
	for (const FLSCombatAttackPresentation& Presentation : AttackFeedback)
	{
		const FLSAttackExecution& Attack = Presentation.Execution;
		if (!IsFeedbackPointVisible(Attack.SourceLocation) || !IsFeedbackPointVisible(Attack.TargetLocation)) continue;
		DrawWorldLine(Attack.SourceLocation, Attack.SourceLocation + Attack.Direction.GetSafeNormal() * 55,
			FLinearColor::White, 3);
	}
	for (const FLSCombatDamagePresentation& Presentation : DamageFeedback)
	{
		const FLSDamageResult& Damage = Presentation.Damage;
		FVector2D Impact;
		if (Damage.bHasImpactLocation && IsFeedbackPointVisible(Damage.ImpactLocation) &&
		    PlayerOwner->ProjectWorldLocationToScreen(Damage.ImpactLocation, Impact))
		{
			// 흰 몸체·밝은 지면에서도 실제 피격을 구분하는 화면 크기 고정 표시다.
			for (const float Sign : {-1.f, 1.f})
			{
				DrawLine(Impact.X - 6, Impact.Y - 6 * Sign, Impact.X + 6, Impact.Y + 6 * Sign,
					FLinearColor::Black, 7);
				DrawLine(Impact.X - 6, Impact.Y - 6 * Sign, Impact.X + 6, Impact.Y + 6 * Sign,
					FLinearColor::White, 3);
			}
		}
		const auto* Faction = Cast<ILSFactionInterface>(Damage.Receiver.Get());
		if (!Faction || Faction->GetFaction() != ELSFaction::Ally || !Damage.bHasIncomingDirection ||
		    !Damage.bHasAttackOrigin || !IsFeedbackPointVisible(Damage.AttackOrigin) ||
		    !Damage.bHasImpactLocation || !IsFeedbackPointVisible(Damage.ImpactLocation)) continue;
		DrawWorldLine(Damage.ImpactLocation, Damage.ImpactLocation - Damage.IncomingDirection.GetSafeNormal() * 75,
			FLinearColor(1, 0.25f, 0.1f), 3);
	}
	const auto* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (AActor* Target = Controller ? Controller->GetAttackCommandMarker() : nullptr; IsFeedbackVisible(Target))
	{
		FVector2D Center;
		if (PlayerOwner->ProjectWorldLocationToScreen(GetVisualBodyBounds(Target).GetCenter(), Center))
		{
			DrawLine(Center.X - 9, Center.Y, Center.X + 9, Center.Y, FLinearColor::Red, 2);
			DrawLine(Center.X, Center.Y - 9, Center.X, Center.Y + 9, FLinearColor::Red, 2);
		}
	}
	for (AActor* Actor : Selection)
		if (const auto* Orders = Actor->FindComponentByClass<ULSOrderComponent>(); Orders && Orders->GetLastFailure() == ELSOrderFailure::BlockedByBarricade)
			if (auto* Barricade = Orders->GetBlockingBarricade(); IsFeedbackVisible(Barricade) && Barricade->IsObstacleActive())
				DrawSelectionRing(Barricade, FLinearColor(1, 0.5f, 0));
	for (const auto& Weak : Projectiles)
	{
		const ALSBasicProjectile* Projectile = Weak.Get();
		if (!Projectile || !Projectile->IsInFlight()) continue;
		const FVector Head = Projectile->GetActorLocation();
		const float TailLength = FMath::Min(168.0f, static_cast<float>(FVector::Distance(Head, Projectile->GetFlightOrigin())));
		ELSFaction ShotFaction;
		const FLinearColor FactionColor = Projectile->GetFlightFaction(ShotFaction)
			? (ShotFaction == ELSFaction::Ally ? FLinearColor(0.08f, 0.55f, 1) : FLinearColor(0.95f, 0.12f, 0.05f))
			: FLinearColor::White;
		const FLinearColor Color = Projectile->GetVisualStyle() == ELSProjectileVisualStyle::TurretBolt ? FLinearColor(1, 0.4f, 0.05f)
			: Projectile->GetVisualStyle() == ELSProjectileVisualStyle::BallistaBolt ? FLinearColor(0.2f, 0.85f, 1) : FactionColor;
		DrawClippedProjectileTail(Head, Projectile->GetFlightDirection(), TailLength, Color);
	}
	for (const auto& Weak : CastleProjectiles)
	{
		const ALSCastleProjectile* Projectile = Weak.Get();
		if (!Projectile || !Projectile->IsInFlight()) continue;
		TArray<TPair<FVector, FVector>> Segments;
		Projectile->GetFlightSegments(Segments);
		const FLinearColor Color = Projectile->GetWeapon() == ELSCastleWeapon::Cannon ? FLinearColor(1, 0.65f, 0.15f) : FLinearColor(1, 0.95f, 0.65f);
		for (const auto& Segment : Segments)
			DrawClippedProjectileTail(Segment.Value, Segment.Value - Segment.Key,
				FMath::Min(60.0f, static_cast<float>(FVector::Distance(Segment.Value, Projectile->GetFlightOrigin()))), Color);
	}

}

void ALSRTSHUD::DrawClippedProjectileTail(const FVector& Head, const FVector& Direction, float Length, const FLinearColor& Color)
{
	const FVector Tail = Head - Direction.GetSafeNormal() * Length;
	const auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	if (!Vision || !Vision->IsVisionEnabled()) { DrawWorldLine(Tail, Head, Color, 3); return; }
	const FVector2D Delta(Head - Tail);
	const double A = Delta.SizeSquared();
	if (A <= UE_SMALL_NUMBER) return;
	TArray<FVector2D> Intervals;
	for (const FLSSightCircle& Circle : Vision->GetSightCircles())
	{
		const FVector2D Offset = FVector2D(Tail) - Circle.Center;
		const double B = 2 * FVector2D::DotProduct(Offset, Delta);
		const double C = Offset.SizeSquared() - Circle.Radius * Circle.Radius;
		const double Discriminant = B * B - 4 * A * C;
		if (Discriminant < 0) continue;
		const double Root = FMath::Sqrt(Discriminant);
		const double Min = FMath::Max(0.0, (-B - Root) / (2 * A));
		const double Max = FMath::Min(1.0, (-B + Root) / (2 * A));
		if (Min < Max) Intervals.Add(FVector2D(Min, Max));
	}
	Intervals.Sort([](const FVector2D& Left, const FVector2D& Right) { return Left.X < Right.X; });
	for (int32 Index = 0; Index < Intervals.Num(); ++Index)
	{
		FVector2D Interval = Intervals[Index];
		while (Index + 1 < Intervals.Num() && Intervals[Index + 1].X <= Interval.Y)
			Interval.Y = FMath::Max(Interval.Y, Intervals[++Index].Y);
		DrawWorldLine(FMath::Lerp(Tail, Head, Interval.X), FMath::Lerp(Tail, Head, Interval.Y), Color, 3);
	}
}
