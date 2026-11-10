#include "Controls/LSPlayerController.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSPathObstacle.h"
#include "Controls/LSCommandCardComponent.h"

#include "Castle/LSCastle.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSRTSHUD.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Vision/LSVisionSubsystem.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#if WITH_EDITOR
#include "Framework/Application/IInputProcessor.h"
#endif

namespace
{
TAutoConsoleVariable<int32> CVarRTSInputDebug(TEXT("LS.RTS.InputDebug"), 0, TEXT("RTS 선택 입력 진단"));
#if WITH_EDITOR
class FLSPendingInputCancelProcessor final : public IInputProcessor
{
  public:
	explicit FLSPendingInputCancelProcessor(TFunction<bool()> InCancelInput)
	    : CancelPendingInput(MoveTemp(InCancelInput)) {}
	virtual void Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override
	{
		return Event.GetKey() == EKeys::Escape && !Event.IsRepeat() && CancelPendingInput();
	}

  private:
	TFunction<bool()> CancelPendingInput;
};
#endif
}

ALSPlayerController::ALSPlayerController()
{
	CommandCard = CreateDefaultSubobject<ULSCommandCardComponent>(TEXT("CommandCard"));
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	Production = CreateDefaultSubobject<ULSTroopProductionComponent>(TEXT("Production"));
}

bool ALSPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (UsesDirectCastleControl() &&
	    (Params.Key == EKeys::W || Params.Key == EKeys::A || Params.Key == EKeys::S || Params.Key == EKeys::D))
		return Super::InputKey(Params);
	if (Params.Event == IE_Pressed && CanUseWorldInput(true, true))
	{
		if (Params.Key == EKeys::Tab)
		{
			CancelInput();
			CommandCard->CycleGroup();
			return true;
		}
		const FModifierKeysState Modifiers = FSlateApplication::Get().GetModifierKeys();
		if (!Modifiers.IsShiftDown() && !Modifiers.IsControlDown() && !Modifiers.IsAltDown() &&
		    !Modifiers.IsCommandDown())
		{
			const FKey WeaponKeys[] = {
			    UsesDirectCastleControl() ? EKeys::One : EKeys::Q,
			    UsesDirectCastleControl() ? EKeys::Two : EKeys::W,
			    UsesDirectCastleControl() ? EKeys::Three : EKeys::E};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(WeaponKeys); ++Index)
				if (Params.Key == WeaponKeys[Index])
				{
					ArmCastleWeapon(static_cast<ELSCastleWeapon>(Index));
					return true;
				}
		}
		// 무장 키는 준비 실패·보조 키 조합에서도 선택 카드로 다시 전달하지 않는다.
		if (Params.Key == EKeys::Q || Params.Key == EKeys::W || Params.Key == EKeys::E)
			return true;
		if (CommandCard->HandleHotkey(Params.Key))
			return true;
	}
	if (Params.Event == IE_Pressed && CanUseWorldInput(false, true))
	{
		const FKey Numbers[] = {EKeys::Zero, EKeys::One, EKeys::Two,   EKeys::Three, EKeys::Four,
		                        EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
		for (int32 Number = 0; Number < UE_ARRAY_COUNT(Numbers); ++Number)
		{
			if (Params.Key == Numbers[Number])
			{
				// Ctrl가 같은 프레임에 떼어져도 부대 지정 입력을 잃지 않는다.
				const bool bAssign = FSlateApplication::Get().GetModifierKeys().IsControlDown();
				UseControlGroup(Number, bAssign);
				if (CVarRTSInputDebug.GetValueOnGameThread())
				{
					UE_LOG(LogTemp, Display, TEXT("RTS_INPUT group=%d assign=%d selected=%d"), Number, bAssign,
					       GetSelectedActors().Num());
				}
				return true;
			}
		}
	}
	if (Params.Key == EKeys::LeftMouseButton && Params.Event == IE_Pressed)
	{
		float X;
		float Y;
		// 프레임 안에 드래그가 끝나도 누른 시점의 좌표가 끝점으로 바뀌지 않게 한다.
		bHasPressedMousePosition = GetMousePosition(X, Y);
		if (bHasPressedMousePosition)
		{
			PressedMousePosition = FVector2D(X, Y);
		}
	}
	return Super::InputKey(Params);
}

void ALSPlayerController::ProduceTroop(ELSTroopType Type)
{
	CancelInput();
	if (LSBattleOutcome::AreActionsStopped(GetWorld()) || !GetSelectedArmedCastle())
	{
		LastProductionResult = ELSProductionResult::Unavailable;
		return;
	}
	LastProductionResult = Production->Produce(Type);
}

ALSCastle* ALSPlayerController::GetSelectedArmedCastle() const
{
	for (AActor* Actor : GetSelectedActors())
	{
		ALSCastle* Castle = Cast<ALSCastle>(Actor);
		if (Castle && Castle->GetFaction() == ELSFaction::Ally && Castle->Selection->CanPlayerControl() &&
		    ULSCombatTargetLibrary::IsCombatAlive(Castle))
			return Castle;
	}
	return nullptr;
}

bool ALSPlayerController::UsesBattlefieldCastle() const
{
	if (GetWorld())
	{
		for (TActorIterator<ALSBattlefieldSetup> It(GetWorld()); It; ++It)
			bRequiresBattlefieldCastle = true;
		if (!bRequiresBattlefieldCastle)
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				if (It->FindComponentByClass<ULSEnemyBehaviorComponent>())
				{
					bRequiresBattlefieldCastle = true;
					break;
				}
	}
	return bRequiresBattlefieldCastle;
}

ALSCastle* ALSPlayerController::GetConfiguredBattlefieldCastle() const
{
	const UWorld* World = GetWorld();
	if (!World || bControllerEnded || World->bIsTearingDown ||
	    (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
		return nullptr;
	int32 SetupCount = 0;
	ALSBattlefieldSetup* Setup = nullptr;
	for (TActorIterator<ALSBattlefieldSetup> It(World); It; ++It)
	{
		Setup = *It;
		++SetupCount;
	}
	bRequiresBattlefieldCastle |= SetupCount > 0;
	const auto* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	FLSPlayerCastleState Snapshot;
	if (SetupCount != 1 || !State || !State->IsSetupValid() || !State->TryGetPlayerCastleState(Snapshot) ||
	    Snapshot.Castle.Get() != Setup->PlayerCastle.Get() || !Setup->GetConfigurationError().IsEmpty())
		return nullptr;
	auto* Castle = Cast<ALSCastle>(Snapshot.Castle.Get());
	return IsValid(Castle) && !Castle->IsActorBeingDestroyed() && Castle->GetWorld() == World &&
	               Castle->GetFaction() == ELSFaction::Ally
	           ? Castle : nullptr;
}

ALSCastle* ALSPlayerController::GetCastleWeaponSource() const
{
	const UWorld* World = GetWorld();
	if (!World || bControllerEnded || World->bIsTearingDown ||
	    (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE) ||
	    LSBattleOutcome::AreActionsStopped(World))
		return nullptr;
	ALSCastle* Castle = GetConfiguredBattlefieldCastle();
	if (const auto* State = World->GetSubsystem<ULSBattlefieldStateSubsystem>();
	    State && !LSBattleOutcome::IsManaged(World) && State->IsBattleEndedForEnemies())
		return nullptr;
	if (!Castle && !UsesBattlefieldCastle())
		Castle = GetSelectedArmedCastle();
	return IsValid(Castle) && Castle->GetWorld() == World && !Castle->IsActorBeingDestroyed() &&
	               Castle->Selection->CanPlayerControl() && ULSCombatTargetLibrary::IsCombatAlive(Castle)
	           ? Castle : nullptr;
}

ALSCastle* ALSPlayerController::GetAimingCastle() const
{
	return PendingInput == ELSCommandInput::CastleWeapon && AimingCastle.IsValid() &&
	               GetCastleWeaponSource() == AimingCastle.Get()
	           ? AimingCastle.Get() : nullptr;
}

bool ALSPlayerController::ArmCastleWeapon(ELSCastleWeapon Weapon)
{
	CancelInput();
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		LastWeaponResult = ELSWeaponCastResult::Unavailable;
		return false;
	}
	ALSCastle* Castle = GetCastleWeaponSource();
	LastWeaponResult = Castle ? Castle->Weapons->GetStatus(Weapon) : ELSWeaponCastResult::Unavailable;
	if (LastWeaponResult != ELSWeaponCastResult::Ready)
		return false;
	AimingCastle = Castle;
	Castle->Weapons->OnWeaponStateChanged.AddDynamic(this, &ALSPlayerController::OnAimedWeaponStateChanged);
	PendingWeapon = Weapon;
	PendingInput = ELSCommandInput::CastleWeapon;
	return true;
}

ELSWeaponCastResult ALSPlayerController::ConfirmCastleWeapon(const FVector& Aim)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()) || PendingInput != ELSCommandInput::CastleWeapon ||
	    !GetAimingCastle())
	{
		CancelInput();
		LastWeaponResult = ELSWeaponCastResult::Unavailable;
		return LastWeaponResult;
	}
	LastWeaponResult = AimedObstacle.IsValid()
	                       ? AimingCastle->Weapons->CastWeaponAtObstacle(PendingWeapon, AimedObstacle.Get())
	                       : AimingCastle->Weapons->CastWeapon(PendingWeapon, Aim);
	if (LastWeaponResult == ELSWeaponCastResult::Ready)
		CancelInput();
	return LastWeaponResult;
}

bool ALSPlayerController::GetCastleWeaponAim(FVector& Aim) const
{
	AimedObstacle.Reset();
	if (!GetAimingCastle() || !GetWorld())
		return false;
	if (const ALSRTSHUD* HUD = Cast<ALSRTSHUD>(GetHUD()))
	{
		if (HUD->GetMinimapWorldPoint(Aim))
			return true;
	}
	FHitResult ObstacleHit;
	if (HitWorld(ObstacleHit))
	{
		if (ALSPathObstacle* Obstacle = Cast<ALSPathObstacle>(ObstacleHit.GetActor()))
		{
			if (AimingCastle->Weapons->GetObstacleAimPoint(Obstacle, Aim))
			{
				AimedObstacle = Obstacle;
				return true;
			}
		}
	}
	FVector Origin, Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction))
		return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * 100000, Objects))
		return false;
	Aim = Hit.ImpactPoint;
	return true;
}

void ALSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	for (bool bShift : {false, true})
	{
		InputComponent->BindKey(FInputChord(EKeys::LeftMouseButton, bShift, false, false, false), IE_Pressed, this,
		                        &ALSPlayerController::OnSelectPressed);
		InputComponent->BindKey(FInputChord(EKeys::LeftMouseButton, bShift, false, false, false), IE_Released, this,
		                        &ALSPlayerController::OnSelectReleased);
		InputComponent->BindKey(FInputChord(EKeys::LeftMouseButton, bShift, false, false, false), IE_DoubleClick, this,
		                        &ALSPlayerController::SelectSameType);
		InputComponent->BindKey(FInputChord(EKeys::RightMouseButton, bShift, false, false, false), IE_Pressed, this,
		                        &ALSPlayerController::OnRightClick);
		InputComponent->BindKey(FInputChord(EKeys::A, bShift, false, false, false), IE_Pressed, this,
		                        &ALSPlayerController::ArmAttack);
		InputComponent->BindKey(FInputChord(EKeys::M, bShift, false, false, false), IE_Pressed, this,
		                        &ALSPlayerController::ArmMove);
		InputComponent->BindKey(FInputChord(EKeys::Escape, bShift, false, false, false), IE_Pressed, this,
		                        &ALSPlayerController::CancelInput);
	}
	InputComponent->BindKey(EKeys::S, IE_Pressed, this, &ALSPlayerController::StopSelected);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ALSPlayerController::HoldSelected);
	InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &ALSPlayerController::ZoomCamera);
	InputComponent->BindKey(EKeys::Home, IE_Pressed, this, &ALSPlayerController::FocusSelection);
}

void ALSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	UsesBattlefieldCastle();
	if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
	{
		Vision->OnSnapshotCommittedEvent.AddDynamic(this, &ALSPlayerController::PruneSelection);
	}
	if (IsLocalController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
#if WITH_EDITOR
		if (GetWorld()->WorldType == EWorldType::PIE && FSlateApplication::IsInitialized())
		{
			// PIE의 종료 단축키는 PlayerController보다 먼저 실행되므로 준비 입력의 취소만 앞서 처리한다.
			const TWeakObjectPtr<ALSPlayerController> WeakController(this);
			PIECancelInputProcessor = MakeShared<FLSPendingInputCancelProcessor>([WeakController]()
			{
				ALSPlayerController* Controller = WeakController.Get();
				if (!Controller || Controller->bControllerEnded || !Controller->GetWorld() ||
				    Controller->GetWorld()->bIsTearingDown || Controller->GetWorld()->WorldType != EWorldType::PIE ||
				    Controller->GetPendingInput() == ELSCommandInput::None ||
				    !Controller->CanUseWorldInput(true, true))
					return false;
				Controller->CancelInput();
				return true;
			});
			FSlateApplication::Get().RegisterInputPreProcessor(PIECancelInputProcessor);
		}
#endif
		if (Cast<ALSRTSCameraPawn>(GetPawn()))
		{
			SetViewTarget(GetPawn());
		}
	}
}

bool ALSPlayerController::CanUseWorldInput(bool bAllowHUD, bool bKeyboard) const
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	const UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!IsLocalController() || !Client || !Client->Viewport || !Client->Viewport->HasFocus() ||
	    !Client->Viewport->IsForegroundWindow() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(
	    FSlateApplication::Get().GetCursorPos(), FSlateApplication::Get().GetInteractiveTopLevelWindows());
	// 새 PIE 창은 SPIEViewport를 사용하므로 타입 이름 대신 실제 전장 위젯을 비교한다.
	const TSharedPtr<SViewport> GameViewport = Client->GetGameViewportWidget();
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (bKeyboard && Focused.IsValid() && Focused != GameViewport)
		return false;
	const ALSRTSHUD* HUD = Cast<ALSRTSHUD>(GetHUD());
	bool bMinimapPath = false;
	bool bBattleHUDPath = false;
	if (HUD && Path.IsValid())
	{
		for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
		{
			bMinimapPath |= HUD->IsMinimapWidget(Path.Widgets[Index].Widget);
			bBattleHUDPath |= HUD->IsBattleWidget(Path.Widgets[Index].Widget);
		}
	}
	const bool bMinimap = HUD && bMinimapPath;
	const bool bCaptured = HUD && HUD->IsMinimapPointerCaptured();
	if (!bKeyboard && bCaptured)
	{
		return false;
	}
	if (!Path.IsValid() || !GameViewport.IsValid() ||
	    (Path.Widgets.Last().Widget != GameViewport && !(bKeyboard && (bMinimap || bCaptured || bBattleHUDPath))))
	{
		return false;
	}
	// UMG 위에서는 전장 뷰포트의 마우스 좌표가 없을 수 있으므로 실제 Slate 경로를 사용한다.
	if (bKeyboard && (bMinimap || bCaptured || bBattleHUDPath))
		return true;
	float X;
	float Y;
	int32 Width;
	int32 Height;
	GetViewportSize(Width, Height);
	if (!GetMousePosition(X, Y) || X < 0 || Y < 0 || X >= Width || Y >= Height)
	{
		return false;
	}
	return bAllowHUD || !HUD || !HUD->IsOverPanel(FVector2D(X, Y));
}

void ALSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bControllerEnded = true;
#if WITH_EDITOR
	if (PIECancelInputProcessor && FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(PIECancelInputProcessor);
	PIECancelInputProcessor.Reset();
#endif
	CancelInput();
	if (ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
	{
		Vision->OnSnapshotCommittedEvent.RemoveDynamic(this, &ALSPlayerController::PruneSelection);
	}
	if (HoveredMine.IsValid()) HoveredMine->SetHovered(false);
	HoveredMine.Reset();
	Super::EndPlay(EndPlayReason);
}

void ALSPlayerController::PruneSelection()
{
	SelectedActors.RemoveAll(
	    [](const TWeakObjectPtr<AActor>& Actor)
	    {
		    const ULSSelectableComponent* Selection =
		        Actor.IsValid() ? Actor->FindComponentByClass<ULSSelectableComponent>() : nullptr;
		    return !Selection || !Selection->IsSelectable();
	    });
}

void ALSPlayerController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	ALSMine* NewHover = nullptr;
	FHitResult HoverHit;
	if (CanUseWorldInput() && HitWorld(HoverHit)) NewHover = Cast<ALSMine>(HoverHit.GetActor());
	if (HoveredMine.Get() != NewHover)
	{
		if (HoveredMine.IsValid()) HoveredMine->SetHovered(false);
		HoveredMine = NewHover;
		if (NewHover) NewHover->SetHovered(true);
	}
	PruneSelection();
	if (PendingInput == ELSCommandInput::CastleWeapon &&
	    (!GetAimingCastle() ||
	     AimingCastle->Weapons->GetStatus(PendingWeapon) != ELSWeaponCastResult::Ready))
		CancelInput();
	ALSRTSCameraPawn* CameraPawn = Cast<ALSRTSCameraPawn>(GetPawn());
	if (UsesDirectCastleControl())
	{
		if (!DrivenCastle.IsValid())
			DrivenCastle = GetConfiguredBattlefieldCastle();
		if (ALSCastle* Castle = DrivenCastle.Get())
		{
			if (CameraPawn)
				CameraPawn->SetFollowTarget(Castle);
			if (auto* Movement = Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent()))
			{
				if (!Movement->IsPlayerDriveEnabled())
				{
					Castle->Orders->SubmitOrder(FLSOrder());
					ApplySelection({Castle});
				}
				const bool bDrive = CanUseWorldInput(false, true) &&
				    ULSCombatTargetLibrary::IsCombatAlive(Castle);
				const float Throttle = bDrive ? static_cast<int32>(IsInputKeyDown(EKeys::W)) -
				    static_cast<int32>(IsInputKeyDown(EKeys::S)) : 0;
				const float Steering = bDrive ? static_cast<int32>(IsInputKeyDown(EKeys::D)) -
				    static_cast<int32>(IsInputKeyDown(EKeys::A)) : 0;
				Movement->SetPlayerDriveInput(Throttle, Steering);
			}
		}
		bWasDraggingCamera = false;
		return;
	}
	if (CameraPawn && !CanUseWorldInput() && CanUseWorldInput(false, true))
	{
		const FVector2D Keys(
		    static_cast<int32>(IsInputKeyDown(EKeys::Right)) - static_cast<int32>(IsInputKeyDown(EKeys::Left)),
		    static_cast<int32>(IsInputKeyDown(EKeys::Up)) - static_cast<int32>(IsInputKeyDown(EKeys::Down)));
		CameraPawn->Pan(Keys, DeltaSeconds);
	}
	if (!CameraPawn || !CanUseWorldInput())
	{
		if (bSelecting && CVarRTSInputDebug.GetValueOnGameThread())
		{
			UE_LOG(LogTemp, Display, TEXT("RTS_INPUT drag cancelled by world gate"));
		}
		bWasDraggingCamera = false;
		bSelecting = false;
		return;
	}
	float X;
	float Y;
	GetMousePosition(X, Y);
	const FVector2D Mouse(X, Y);
	const bool bDragging = IsInputKeyDown(EKeys::MiddleMouseButton);
	if (bDragging)
	{
		if (bWasDraggingCamera)
		{
			CameraPawn->Drag(Mouse - PreviousMouse);
		}
	}
	else
	{
		int32 Width;
		int32 Height;
		GetViewportSize(Width, Height);
		const FVector2D Keys(
		    static_cast<int32>(IsInputKeyDown(EKeys::Right)) - static_cast<int32>(IsInputKeyDown(EKeys::Left)),
		    static_cast<int32>(IsInputKeyDown(EKeys::Up)) - static_cast<int32>(IsInputKeyDown(EKeys::Down)));
		CameraPawn->Pan(Keys + (bSelecting ? FVector2D::ZeroVector
		                                   : ALSRTSCameraPawn::CalculateEdgeDirection(Mouse, FVector2D(Width, Height),
		                                                                              EdgeScrollMargin)),
		                DeltaSeconds);
	}
	PreviousMouse = Mouse;
	bWasDraggingCamera = bDragging;
}

void ALSPlayerController::ZoomCamera(float Value)
{
	if (Value != 0.0f && CanUseWorldInput())
	{
		if (ALSRTSCameraPawn* CameraPawn = Cast<ALSRTSCameraPawn>(GetPawn()))
		{
			CameraPawn->Zoom(Value);
		}
	}
}

void ALSPlayerController::FocusSelection()
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	const TArray<AActor*> Selection = GetSelectedActors();
	if (!Selection.IsEmpty())
	{
		FVector Center = FVector::ZeroVector;
		for (AActor* Actor : Selection)
		{
			Center += Actor->GetActorLocation();
		}
		if (ALSRTSCameraPawn* CameraPawn = Cast<ALSRTSCameraPawn>(GetPawn()))
		{
			CameraPawn->FocusLocation(Center / Selection.Num());
		}
	}
}

ALSCastle* ALSPlayerController::GetSelectedCastle() const
{
	for (AActor* Actor : GetSelectedActors())
	{
		if (ALSCastle* Castle = Cast<ALSCastle>(Actor))
		{
			return Castle;
		}
	}
	return nullptr;
}

TArray<AActor*> ALSPlayerController::GetSelectedActors() const
{
	TArray<AActor*> Result;
	for (const TWeakObjectPtr<AActor>& Actor : SelectedActors)
	{
		const ULSSelectableComponent* Selection =
		    Actor.IsValid() ? Actor->FindComponentByClass<ULSSelectableComponent>() : nullptr;
		if (Selection && Selection->IsSelectable())
		{
			Result.Add(Actor.Get());
		}
	}
	return Result;
}

void ALSPlayerController::ApplySelection(const TArray<AActor*>& Actors, bool bToggle, bool bPreferUnits)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	TArray<AActor*> Valid;
	for (AActor* Actor : Actors)
	{
		const ULSSelectableComponent* Selection =
		    IsValid(Actor) ? Actor->FindComponentByClass<ULSSelectableComponent>() : nullptr;
		if (Selection && Selection->IsSelectable())
		{
			Valid.AddUnique(Actor);
		}
	}
	if (bPreferUnits && Valid.ContainsByPredicate(
	                        [](AActor* Actor)
	                        {
		                        const ULSSelectableComponent* S = Actor->FindComponentByClass<ULSSelectableComponent>();
		                        return S->CanPlayerControl() &&
		                               (S->Kind == ELSSelectionKind::Unit || S->Kind == ELSSelectionKind::Castle);
	                        }))
	{
		Valid.RemoveAll(
		    [](AActor* Actor)
		    {
			    const ULSSelectableComponent* S = Actor->FindComponentByClass<ULSSelectableComponent>();
			    return !S->CanPlayerControl() ||
			           (S->Kind != ELSSelectionKind::Unit && S->Kind != ELSSelectionKind::Castle);
		    });
	}
	if (Valid.Num() > 1)
	{
		Valid.RemoveAll([](AActor* Actor)
		                { return !Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl(); });
	}
	if (!bToggle)
	{
		SelectedActors.Reset();
	}
	for (AActor* Actor : Valid)
	{
		const bool bControlled = Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		if (!bControlled)
		{
			if (!bToggle)
			{
				SelectedActors = {Actor};
			}
			continue;
		}
		SelectedActors.RemoveAll(
		    [](const TWeakObjectPtr<AActor>& Item)
		    { return !Item.IsValid() || !Item->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl(); });
		const TWeakObjectPtr<AActor> Weak(Actor);
		if (bToggle && SelectedActors.Contains(Weak))
		{
			SelectedActors.Remove(Weak);
		}
		else
		{
			SelectedActors.AddUnique(Weak);
		}
	}
}

bool ALSPlayerController::IsShiftDown() const
{
	return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

bool ALSPlayerController::HitWorld(FHitResult& Hit, bool bNormalRightClick) const
{
	FVector Origin, Direction;
	if (!GetWorld() || !DeprojectMousePositionToWorld(Origin, Direction))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LSVisibleCursor), false);
	if (const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>())
	{
		TArray<AActor*> Concealed;
		Vision->GetConcealedActors(Concealed);
		for (AActor* Actor : Concealed)
		{
			Params.AddIgnoredActor(Actor);
			TArray<AActor*> ConcealedChildren;
			Actor->FindComponentByClass<ULSVisionParticipantComponent>()->GetAttachedVisualActors(ConcealedChildren);
			Params.AddIgnoredActors(ConcealedChildren);
		}
	}
	if (bNormalRightClick)
	{
		for (TActorIterator<ALSMine> It(GetWorld()); It; ++It)
		{
			const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
			if (!Vision || Vision->IsPointVisible(It->GetTriggerCenter())) continue;
			Params.AddIgnoredActor(*It);
			TArray<AActor*> Visuals;
			It->GetAttachedActors(Visuals, true, true);
			Params.AddIgnoredActors(Visuals);
		}
	}
	return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * HitResultTraceDistance,
	                                            ECC_Visibility, Params);
}

void ALSPlayerController::OnSelectPressed()
{
	if (CVarRTSInputDebug.GetValueOnGameThread())
	{
		float X = -1, Y = -1;
		GetMousePosition(X, Y);
		UE_LOG(LogTemp, Display, TEXT("RTS_INPUT press allowed=%d mouse=%f,%f"), CanUseWorldInput(true), X, Y);
	}
	if (!CanUseWorldInput(true, true))
	{
		return;
	}
	float MouseX, MouseY;
	if (GetMousePosition(MouseX, MouseY))
	{
		if (ALSRTSHUD* HUD = Cast<ALSRTSHUD>(GetHUD()); HUD && HUD->HandleClick(FVector2D(MouseX, MouseY)))
		{
			return;
		}
	}
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
	{
		bHasPressedMousePosition = false;
		return;
	}
	if (PendingInput != ELSCommandInput::None)
	{
		CompleteTargetInput();
		return;
	}
	float X = 0, Y = 0;
	if (bHasPressedMousePosition || GetMousePosition(X, Y))
	{
		bSelecting = true;
		SelectionStart = bHasPressedMousePosition ? PressedMousePosition : FVector2D(X, Y);
	}
	bHasPressedMousePosition = false;
}

bool ALSPlayerController::GetSelectionBox(FVector2D& Start, FVector2D& End) const
{
	float X;
	float Y;
	if (!bSelecting || !GetMousePosition(X, Y))
	{
		return false;
	}
	Start = SelectionStart;
	End = FVector2D(X, Y);
	return FVector2D::Distance(Start, End) >= DragThreshold;
}

void ALSPlayerController::OnSelectReleased()
{
	if (CVarRTSInputDebug.GetValueOnGameThread())
	{
		float X = -1, Y = -1;
		GetMousePosition(X, Y);
		UE_LOG(LogTemp, Display, TEXT("RTS_INPUT release selecting=%d allowed=%d mouse=%f,%f"), bSelecting,
		       CanUseWorldInput(), X, Y);
	}
	if (!bSelecting)
	{
		return;
	}
	FVector2D Start, End;
	const bool bBox = GetSelectionBox(Start, End);
	bSelecting = false;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()) || !CanUseWorldInput())
	{
		return;
	}
	TArray<AActor*> Candidates;
	if (bBox)
	{
		const FBox2D Box(TArray<FVector2D>{Start, End});
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			const ULSSelectableComponent* Selection = It->FindComponentByClass<ULSSelectableComponent>();
			FVector2D Point;
			if (Selection && Selection->CanPlayerControl() &&
			    ProjectWorldLocationToScreen(It->GetActorLocation(), Point) && Box.IsInsideOrOn(Point))
			{
				Candidates.Add(*It);
			}
		}
	}
	else
	{
		FHitResult Hit;
		if (HitWorld(Hit))
		{
			Candidates.Add(Hit.GetActor());
		}
	}
	ApplySelection(Candidates, IsShiftDown(), bBox);
	if (CVarRTSInputDebug.GetValueOnGameThread())
	{
		UE_LOG(LogTemp, Display, TEXT("RTS_INPUT candidates=%d selected=%d box=%d"), Candidates.Num(),
		       GetSelectedActors().Num(), bBox);
	}
}

void ALSPlayerController::SelectSameType()
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	bSelecting = false;
	if (!CanUseWorldInput() || PendingInput != ELSCommandInput::None)
	{
		return;
	}
	FHitResult Hit;
	if (!HitWorld(Hit) || !Hit.GetActor())
	{
		return;
	}
	const ULSSelectableComponent* Source = Hit.GetActor()->FindComponentByClass<ULSSelectableComponent>();
	if (!Source || !Source->CanPlayerControl())
	{
		return;
	}
	int32 Width;
	int32 Height;
	GetViewportSize(Width, Height);
	TArray<AActor*> Candidates;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const ULSSelectableComponent* Selection = It->FindComponentByClass<ULSSelectableComponent>();
		FVector2D Point;
		if (Selection && Selection->CanPlayerControl() && Selection->GetSelectionType() == Source->GetSelectionType() &&
		    ProjectWorldLocationToScreen(It->GetActorLocation(), Point) && Point.X >= 0 && Point.Y >= 0 &&
		    Point.X < Width && Point.Y < Height)
		{
			Candidates.Add(*It);
		}
	}
	ApplySelection(Candidates, IsShiftDown());
}

void ALSPlayerController::ArmAttack()
{
	if (UsesDirectCastleControl()) return;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (CVarRTSInputDebug.GetValueOnGameThread())
		UE_LOG(LogTemp, Display, TEXT("RTS_INPUT attack allowed=%d"), CanUseWorldInput(false, true));
	if (!CanUseWorldInput(false, true))
	{
		return;
	}
	for (AActor* Actor : GetSelectedActors())
	{
		const ULSOrderComponent* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
		if (Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl() && Orders && Orders->CanAttack())
		{
			SetPendingInput(ELSCommandInput::Attack);
			return;
		}
	}
}

void ALSPlayerController::ArmMove()
{
	if (UsesDirectCastleControl()) return;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (!CanUseWorldInput(false, true))
	{
		return;
	}
	for (AActor* Actor : GetSelectedActors())
	{
		const ULSOrderComponent* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
		if (Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl() && Orders && Orders->CanMove())
		{
			SetPendingInput(ELSCommandInput::Move);
			return;
		}
	}
}
void ALSPlayerController::CancelInput()
{
	PendingInput = ELSCommandInput::None;
	if (AimingCastle.IsValid())
		AimingCastle->Weapons->OnWeaponStateChanged.RemoveDynamic(this,
		                                                          &ALSPlayerController::OnAimedWeaponStateChanged);
	AimingCastle.Reset();
	AimedObstacle.Reset();
	bSelecting = false;
}

void ALSPlayerController::SetPendingInput(ELSCommandInput Input)
{
	CancelInput();
	if (!LSBattleOutcome::AreActionsStopped(GetWorld()))
		PendingInput = Input;
}

void ALSPlayerController::EndBattleActions()
{
	if (ALSRTSHUD* HUD = Cast<ALSRTSHUD>(GetHUD())) HUD->EndBattleActions();
	CancelInput();
	bHasPressedMousePosition = false;
	OrderMarkerExpiresAt = -1;
	AttackCommandTarget.Reset();
	AttackCommandMarkerExpiresAt = -1;
	LastProductionResult = ELSProductionResult::Unavailable;
	LastWeaponResult = ELSWeaponCastResult::Unavailable;
}

void ALSPlayerController::OnAimedWeaponStateChanged(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State)
{
	if (PendingInput == ELSCommandInput::CastleWeapon && Weapon == PendingWeapon &&
	    (!State.bUnlocked || !State.bEnabled))
		CancelInput();
}

void ALSPlayerController::CompleteTargetInput()
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (PendingInput == ELSCommandInput::CastleWeapon)
	{
		FVector Aim;
		if (GetCastleWeaponAim(Aim))
			ConfirmCastleWeapon(Aim);
		return;
	}
	FHitResult Hit;
	if (HitWorld(Hit))
	{
		if (Hit.Component.IsValid() && Hit.Component->GetCollisionObjectType() == LSCollisionChannels::Ground)
		{
			IssueSelectionOrder(PendingInput == ELSCommandInput::Attack ? ELSOrderKind::AttackMove : ELSOrderKind::Move,
			                    Hit.ImpactPoint, nullptr, IsShiftDown());
		}
		else if (PendingInput == ELSCommandInput::Attack)
		{
			IssueSelectionOrder(ELSOrderKind::AttackTarget, FVector::ZeroVector, Hit.GetActor(), IsShiftDown());
		}
	}
	if (!IsShiftDown())
	{
		CancelInput();
	}
}

void ALSPlayerController::OnRightClick()
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (PendingInput == ELSCommandInput::CastleWeapon)
	{
		if (CanUseWorldInput(true, true))
			CancelInput();
		return;
	}
	if (!CanUseWorldInput())
	{
		return;
	}
	if (PendingInput != ELSCommandInput::None || bSelecting)
	{
		CancelInput();
		return;
	}
	FHitResult Hit;
	if (!HitWorld(Hit, true))
	{
		return;
	}
	if (Hit.Component.IsValid() && Hit.Component->GetCollisionObjectType() == LSCollisionChannels::Ground)
	{
		IssueSelectionOrder(ELSOrderKind::Move, Hit.ImpactPoint, nullptr, IsShiftDown());
	}
	else
	{
		IssueSelectionOrder(Cast<ALSMine>(Hit.GetActor()) ? ELSOrderKind::Disarm : ELSOrderKind::AttackTarget, FVector::ZeroVector, Hit.GetActor(), IsShiftDown());
	}
}

void ALSPlayerController::StopSelected()
{
	if (UsesDirectCastleControl()) return;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (CanUseWorldInput(false, true))
	{
		CancelInput();
		IssueSelectionOrder(ELSOrderKind::Idle, FVector::ZeroVector, nullptr, false);
	}
}

void ALSPlayerController::HoldSelected()
{
	if (UsesDirectCastleControl()) return;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (CanUseWorldInput(false, true))
	{
		CancelInput();
		IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false);
	}
}

bool ALSPlayerController::IssueSelectionOrder(ELSOrderKind Kind, const FVector& Location, AActor* Target, bool bQueue)
{
	if (UsesDirectCastleControl()) return false;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	if (Kind == ELSOrderKind::Disarm)
	{
		ALSMine* Mine = Cast<ALSMine>(Target);
		if (bQueue || !Mine || !Mine->IsArmed() || Mine->HasDisarmWorker()) return false;
		ULSOrderComponent* Worker = nullptr;
		FVector Destination;
		double BestLength = TNumericLimits<double>::Max();
		TArray<ULSOrderComponent*> Group;
		for (AActor* Actor : GetSelectedActors())
		{
			ULSOrderComponent* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
			if (!Orders || !Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl()) continue;
			Group.Add(Orders);
			FVector Candidate;
			double Length;
			if (Actor->FindComponentByClass<ULSSelectableComponent>()->Kind == ELSSelectionKind::Unit &&
			    Orders->PrepareDisarm(Mine, Candidate, Length) && Length < BestLength)
			{ Worker = Orders; Destination = Candidate; BestLength = Length; }
		}
		if (!Worker || !Mine->TryClaimDisarm(Worker->GetOwner())) return false;
		FLSOrder Hold;
		Hold.Kind = ELSOrderKind::Hold;
		for (ULSOrderComponent* Orders : Group) Orders->SubmitOrder(Hold);
		return Worker->BeginClaimedDisarm(Mine, Destination);
	}
	TArray<AActor*> Controlled;
	float Spacing = FormationSpacing;
	for (AActor* Actor : GetSelectedActors())
	{
		const ULSSelectableComponent* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
		const ULSOrderComponent* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
		if (Selection->CanPlayerControl() && Orders && Orders->CanExecuteOrderKind(Kind))
		{
			Controlled.Add(Actor);
			float Radius;
			float Height;
			Actor->GetSimpleCollisionCylinder(Radius, Height);
			Spacing = FMath::Max(Spacing, Radius * 2 + 40.0f);
		}
	}
	// 일반 이동은 클릭 지점으로 집결한다. 공격 이동의 분산 배치는 유지한다.
	const TArray<FVector> Destinations = Kind == ELSOrderKind::AttackMove
	                                         ? ULSOrderComponent::MakeFormation(Location, Controlled.Num(), Spacing)
	                                         : TArray<FVector>();
	bool bAccepted = false;
	for (int32 Index = 0; Index < Controlled.Num(); ++Index)
	{
		FLSOrder Order;
		Order.Kind = Kind;
		Order.Target = Target;
		Order.Destination = Destinations.IsValidIndex(Index) ? Destinations[Index] : Location;
		bAccepted |= Controlled[Index]->FindComponentByClass<ULSOrderComponent>()->SubmitOrder(Order, bQueue);
	}
	if (bAccepted)
	{
		AttackCommandTarget = Kind == ELSOrderKind::AttackTarget ? Target : nullptr;
		AttackCommandMarkerExpiresAt = GetWorld()->GetTimeSeconds() + AttackCommandMarkerDuration;
	}
	if (bAccepted && (Kind == ELSOrderKind::Move || Kind == ELSOrderKind::AttackMove))
	{
		OrderMarkerLocation = Location;
		OrderMarkerColor = Kind == ELSOrderKind::Move ? FLinearColor::Green : FLinearColor::Red;
		OrderMarkerExpiresAt = GetWorld()->GetTimeSeconds() + 0.7;
	}
	return bAccepted;
}

AActor* ALSPlayerController::GetAttackCommandMarker() const
{
	AActor* Target = AttackCommandTarget.Get();
	const ULSSelectableComponent* Selection = Target ? Target->FindComponentByClass<ULSSelectableComponent>() : nullptr;
	return GetWorld() && GetWorld()->GetTimeSeconds() < AttackCommandMarkerExpiresAt && Selection &&
	               Selection->IsSelectable()
	           ? Target
	           : nullptr;
}

void ALSPlayerController::UseControlGroup(int32 Number, bool bAssign)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	if (Number < 0 || Number > 9)
	{
		return;
	}
	if (bAssign)
	{
		FLSControlGroup& Group = ControlGroups.FindOrAdd(Number);
		Group.Actors.Reset();
		for (AActor* Actor : GetSelectedActors())
		{
			if (Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl())
			{
				Group.Actors.Add(Actor);
			}
		}
		LastGroup = INDEX_NONE;
		return;
	}
	const FLSControlGroup* Group = ControlGroups.Find(Number);
	if (!Group)
	{
		return;
	}
	TArray<AActor*> Valid;
	for (const TWeakObjectPtr<AActor>& Actor : Group->Actors)
	{
		const ULSSelectableComponent* Selection =
		    Actor.IsValid() ? Actor->FindComponentByClass<ULSSelectableComponent>() : nullptr;
		if (Selection && Selection->CanPlayerControl())
		{
			Valid.Add(Actor.Get());
		}
	}
	ApplySelection(Valid);
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastGroup == Number && Now - LastGroupTime <= DoubleGroupInterval)
	{
		FocusSelection();
	}
	LastGroup = Number;
	LastGroupTime = Now;
	CancelInput();
}

bool ALSPlayerController::GetOrderMarker(FVector& OutLocation, FLinearColor& OutColor) const
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() >= OrderMarkerExpiresAt)
	{
		return false;
	}
	OutLocation = OrderMarkerLocation;
	OutColor = OrderMarkerColor;
	return true;
}

bool ALSPlayerController::HandleMinimapInput(const FVector& Location, bool bRightClick, bool bQueue)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return true;
	const ALSBattlefieldSettings* Settings = ALSBattlefieldSettings::Find(GetWorld());
	if (!Settings || Location.ContainsNaN() || !Settings->GetWorldBounds().IsInsideOrOn(FVector2D(Location)))
		return false;
	if (bRightClick && PendingInput != ELSCommandInput::None)
	{
		CancelInput();
		return true;
	}
	const FVector Ground(Location.X, Location.Y, Settings->GetGroundHeight());
	if (!bRightClick && PendingInput == ELSCommandInput::CastleWeapon)
	{
		AimedObstacle.Reset();
		ConfirmCastleWeapon(Ground);
		return true;
	}
	if (bRightClick || PendingInput == ELSCommandInput::Move || PendingInput == ELSCommandInput::Attack)
	{
		const ELSOrderKind Kind =
		    PendingInput == ELSCommandInput::Attack ? ELSOrderKind::AttackMove : ELSOrderKind::Move;
		const bool bAccepted = IssueSelectionOrder(Kind, Ground, nullptr, bQueue);
		if (CVarRTSInputDebug.GetValueOnGameThread())
			UE_LOG(LogTemp, Display, TEXT("RTS_MINIMAP order=%s location=%s selected=%d accepted=%d queue=%d"),
			       *UEnum::GetValueAsString(Kind), *Ground.ToString(), GetSelectedActors().Num(), bAccepted, bQueue);
		if (!bQueue)
			CancelInput();
		return true;
	}
	return false;
}
