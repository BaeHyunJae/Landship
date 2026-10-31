#include "Controls/LSPlayerController.h"

#include "Castle/LSCastle.h"
#include "Castle/LSMobileCastle.h"
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

namespace
{
TAutoConsoleVariable<int32> CVarRTSInputDebug(TEXT("LS.RTS.InputDebug"), 0, TEXT("RTS 선택 입력 진단"));
}

ALSPlayerController::ALSPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	Production = CreateDefaultSubobject<ULSTroopProductionComponent>(TEXT("Production"));
}

bool ALSPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (Params.Event == IE_Pressed && CanUseWorldInput(true))
	{
		const FKey WeaponKeys[] = {EKeys::Q, EKeys::W, EKeys::E};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(WeaponKeys); ++Index)
		{
			if (Params.Key == WeaponKeys[Index])
			{
				ArmCastleWeapon(static_cast<ELSCastleWeapon>(Index));
				return true;
			}
		}
		const FKey ProductionKeys[] = {EKeys::F1, EKeys::F2, EKeys::F3};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ProductionKeys); ++Index)
		{
			if (Params.Key == ProductionKeys[Index])
			{
				ProduceTroop(static_cast<ELSTroopType>(Index));
				return true;
			}
		}
	}
	if (Params.Event == IE_Pressed && CanUseWorldInput())
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
	LastProductionResult = Production->Produce(Type);
}

ALSMobileCastle* ALSPlayerController::GetSelectedArmedCastle() const
{
	for (AActor* Actor : GetSelectedActors())
	{
		ALSMobileCastle* Castle = Cast<ALSMobileCastle>(Actor);
		if (Castle && Castle->GetFaction() == ELSFaction::Ally && Castle->Selection->CanPlayerControl() &&
		    ULSCombatTargetLibrary::IsCombatAlive(Castle))
			return Castle;
	}
	return nullptr;
}

bool ALSPlayerController::ArmCastleWeapon(ELSCastleWeapon Weapon)
{
	CancelInput();
	ALSMobileCastle* Castle = GetSelectedArmedCastle();
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
	if (PendingInput != ELSCommandInput::CastleWeapon || !AimingCastle.IsValid() ||
	    GetSelectedArmedCastle() != AimingCastle.Get())
	{
		CancelInput();
		LastWeaponResult = ELSWeaponCastResult::Unavailable;
		return LastWeaponResult;
	}
	LastWeaponResult = AimingCastle->Weapons->CastWeapon(PendingWeapon, Aim);
	if (LastWeaponResult == ELSWeaponCastResult::Ready)
		CancelInput();
	return LastWeaponResult;
}

bool ALSPlayerController::GetCastleWeaponAim(FVector& Aim) const
{
	if (PendingInput != ELSCommandInput::CastleWeapon || !AimingCastle.IsValid() ||
	    GetSelectedArmedCastle() != AimingCastle.Get() || !GetWorld())
		return false;
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
	if (IsLocalController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		if (Cast<ALSRTSCameraPawn>(GetPawn()))
		{
			SetViewTarget(GetPawn());
		}
	}
}

bool ALSPlayerController::CanUseWorldInput(bool bAllowHUD) const
{
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
	if (!Path.IsValid() || !GameViewport.IsValid() || Path.Widgets.Last().Widget != GameViewport)
	{
		return false;
	}
	float X;
	float Y;
	int32 Width;
	int32 Height;
	GetViewportSize(Width, Height);
	if (!GetMousePosition(X, Y) || X < 0 || Y < 0 || X >= Width || Y >= Height)
	{
		return false;
	}
	const ALSRTSHUD* HUD = Cast<ALSRTSHUD>(GetHUD());
	return bAllowHUD || !HUD || !HUD->IsOverPanel(FVector2D(X, Y));
}

void ALSPlayerController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	if (PendingInput == ELSCommandInput::CastleWeapon &&
	    (!AimingCastle.IsValid() || GetSelectedArmedCastle() != AimingCastle.Get() ||
	     AimingCastle->Weapons->GetStatus(PendingWeapon) != ELSWeaponCastResult::Ready))
		CancelInput();
	SelectedActors.RemoveAll(
	    [](const TWeakObjectPtr<AActor>& Actor)
	    {
		    const ULSSelectableComponent* Selection =
		        Actor.IsValid() ? Actor->FindComponentByClass<ULSSelectableComponent>() : nullptr;
		    return !Selection || !Selection->IsSelectable();
	    });
	ALSRTSCameraPawn* CameraPawn = Cast<ALSRTSCameraPawn>(GetPawn());
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

bool ALSPlayerController::HitWorld(FHitResult& Hit) const
{
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit);
}

void ALSPlayerController::OnSelectPressed()
{
	if (CVarRTSInputDebug.GetValueOnGameThread())
	{
		float X = -1, Y = -1;
		GetMousePosition(X, Y);
		UE_LOG(LogTemp, Display, TEXT("RTS_INPUT press allowed=%d mouse=%f,%f"), CanUseWorldInput(true), X, Y);
	}
	if (!CanUseWorldInput(true))
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
	if (!CanUseWorldInput())
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
	if (!CanUseWorldInput())
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
	if (!CanUseWorldInput())
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
	bSelecting = false;
}

void ALSPlayerController::OnAimedWeaponStateChanged(ELSCastleWeapon Weapon, const FLSCastleWeaponState& State)
{
	if (PendingInput == ELSCommandInput::CastleWeapon && Weapon == PendingWeapon &&
	    (!State.bUnlocked || !State.bEnabled))
		CancelInput();
}

void ALSPlayerController::CompleteTargetInput()
{
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
	if (PendingInput == ELSCommandInput::CastleWeapon)
	{
		if (CanUseWorldInput(true))
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
	if (!HitWorld(Hit))
	{
		return;
	}
	if (Hit.Component.IsValid() && Hit.Component->GetCollisionObjectType() == LSCollisionChannels::Ground)
	{
		IssueSelectionOrder(ELSOrderKind::Move, Hit.ImpactPoint, nullptr, IsShiftDown());
	}
	else
	{
		IssueSelectionOrder(ELSOrderKind::AttackTarget, FVector::ZeroVector, Hit.GetActor(), IsShiftDown());
	}
}

void ALSPlayerController::StopSelected()
{
	if (CanUseWorldInput())
	{
		CancelInput();
		IssueSelectionOrder(ELSOrderKind::Idle, FVector::ZeroVector, nullptr, false);
	}
}

void ALSPlayerController::HoldSelected()
{
	if (CanUseWorldInput())
	{
		CancelInput();
		IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false);
	}
}

bool ALSPlayerController::IssueSelectionOrder(ELSOrderKind Kind, const FVector& Location, AActor* Target, bool bQueue)
{
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
	if (bAccepted && (Kind == ELSOrderKind::Move || Kind == ELSOrderKind::AttackMove))
	{
		OrderMarkerLocation = Location;
		OrderMarkerColor = Kind == ELSOrderKind::Move ? FLinearColor::Green : FLinearColor::Red;
		OrderMarkerExpiresAt = GetWorld()->GetTimeSeconds() + 0.7;
	}
	return bAccepted;
}

void ALSPlayerController::UseControlGroup(int32 Number, bool bAssign)
{
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
