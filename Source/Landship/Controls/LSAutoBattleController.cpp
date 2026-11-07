#include "Controls/LSAutoBattleController.h"
#include "Controls/LSFreeCameraPawn.h"
#include "HUD/LSAutoBattleWidget.h"
#include "Combat/LSCollisionChannels.h"
#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"

ALSAutoBattleController::ALSAutoBattleController()
{
	bShowMouseCursor = true;
	WidgetClass = TSoftClassPtr<ULSAutoBattleWidget>(
	    FSoftObjectPath(TEXT("/Game/Developers/HMH/TABS/WBP_AutoBattle.WBP_AutoBattle_C")));
}

void ALSAutoBattleController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	SetCameraCapture(false);
	if (UClass* Class = WidgetClass.LoadSynchronous())
	{
		Widget = CreateWidget<ULSAutoBattleWidget>(this, Class);
		if (Widget)
		{
			Widget->AddToViewport();
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("자동 전투 WBP가 없습니다. 전용 맵 준비 절차를 실행하세요."));
	}
	if (ALSAutoBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		Mode->OnChanged.AddDynamic(this, &ThisClass::OnBattleChanged);
	}
}

void ALSAutoBattleController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ThisClass::LeftClick);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ThisClass::EndBrushStroke);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ThisClass::LookPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ThisClass::LookReleased);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ThisClass::PanPressed);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &ThisClass::PanReleased);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::Escape);
	InputComponent->BindKey(EKeys::Home, IE_Pressed, this, &ThisClass::Home);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ThisClass::WheelUp);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ThisClass::WheelDown);
	InputComponent->BindKey(EKeys::Delete, IE_Pressed, this, &ThisClass::ToggleEraseBrush);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ThisClass::StartHotkey);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ThisClass::ArmCannon);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ThisClass::ArmShotgun);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ThisClass::ArmBallista);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ThisClass::ArmSwamp);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ThisClass::ArmStim);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ThisClass::ArmWall);
}

void ALSAutoBattleController::SetCameraCapture(bool bCapture)
{
	if (bCapture)
	{
		EndBrushStroke();
	}
	const bool bWasCaptured = bCameraCaptured;
	const UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const bool bCanRestoreCursor = ViewportClient && ViewportClient->Viewport && ViewportClient->Viewport->HasFocus() &&
	                               ViewportClient->Viewport->IsForegroundWindow();
	if (bCapture && !bWasCaptured)
	{
		float X, Y;
		bHasCaptureCursor = GetMousePosition(X, Y);
		if (bHasCaptureCursor)
		{
			CaptureCursor = FVector2D(X, Y);
		}
	}
	bCameraCaptured = bCapture;
	bShowMouseCursor = !bCapture;
	if (bCapture)
	{
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(false);
		SetInputMode(Mode);
	}
	else
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture);
		SetInputMode(Mode);
		if (bWasCaptured && bHasCaptureCursor && bCanRestoreCursor)
		{
			SetMouseLocation(FMath::RoundToInt(CaptureCursor.X), FMath::RoundToInt(CaptureCursor.Y));
		}
		bHasCaptureCursor = false;
		bPanning = false;
	}
}

void ALSAutoBattleController::OnBattleChanged()
{
	ALSAutoBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	if (!Mode)
	{
		return;
	}
	const ELSAutoBattlePhase Phase = Mode->GetPhase();
	FString PresetMessage = Mode->TakePresetLoadMessage();
	if (!PresetMessage.IsEmpty())
		EditorMessage = MoveTemp(PresetMessage);
	if (Phase != LastPhase || LastBattleId != Mode->GetBattleId())
	{
		EndBrushStroke();
		bEraseBrush = false;
		SelectionId = 0;
		bCastlePlacement = false;
		CancelCastleAim();
		SetCameraCapture(false);
		if (auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn()))
		{
			if (Phase == ELSAutoBattlePhase::Fighting && IsValid(Mode->GetAllyCastle()))
				Camera->FollowCastle(*Mode->GetAllyCastle(), 0, true);
			else if (Phase == ELSAutoBattlePhase::Preparation && LastPhase != ELSAutoBattlePhase::Preparation)
				Camera->ResetView();
		}
		if (Phase == ELSAutoBattlePhase::Fighting && IsValid(Mode->GetAllyCastle()) &&
		    Mode->GetAllyCastle()->Weapons->GetStatus(Mode->GetWeapon()) == ELSWeaponCastResult::Ready)
		{
			ArmedWeapon = Mode->GetWeapon();
		}
		LastPhase = Phase;
		LastBattleId = Mode->GetBattleId();
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}

void ALSAutoBattleController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	const auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn());
	const bool bFighting = Mode && Mode->GetPhase() == ELSAutoBattlePhase::Fighting;
	ALSAutoBattleCastle* Castle = bFighting ? Mode->GetAllyCastle() : nullptr;
	if (Camera && IsValid(Castle))
	{
		if (ArmedField.IsSet())
			Camera->UpdateTacticalView(*Castle, DeltaSeconds);
		else
			Camera->FollowCastle(*Castle, DeltaSeconds, false);
	}
	if (!IsLocalController() || !GetWorld()->GetGameViewport() || !GetWorld()->GetGameViewport()->Viewport ||
	    !GetWorld()->GetGameViewport()->Viewport->HasFocus() ||
	    !GetWorld()->GetGameViewport()->Viewport->IsForegroundWindow())
	{
		EndBrushStroke();
		if (bCameraCaptured)
		{
			SetCameraCapture(false);
		}
		return;
	}
	if (!Camera)
	{
		return;
	}
	if (bFighting)
	{
		if (ULSCombatTargetLibrary::IsCombatAlive(Castle))
		{
			const float Throttle = static_cast<int32>(IsInputKeyDown(EKeys::W)) - static_cast<int32>(IsInputKeyDown(EKeys::S));
			const float Steering = static_cast<int32>(IsInputKeyDown(EKeys::D)) - static_cast<int32>(IsInputKeyDown(EKeys::A));
			Castle->Drive(Throttle, Steering, DeltaSeconds);
			UpdateCastleAim(*Castle);
		}
		return;
	}
	if (bCameraCaptured)
	{
		if (!IsInputKeyDown(EKeys::RightMouseButton) && !IsInputKeyDown(EKeys::MiddleMouseButton))
		{
			SetCameraCapture(false);
			return;
		}
		float X, Y;
		GetInputMouseDelta(X, Y);
		if (bPanning)
		{
			Camera->Pan(FVector2D(X, Y));
		}
		else
		{
			Camera->Look(FVector2D(X, Y));
		}
		const FVector Movement((IsInputKeyDown(EKeys::W) ? 1 : 0) - (IsInputKeyDown(EKeys::S) ? 1 : 0),
		                       (IsInputKeyDown(EKeys::D) ? 1 : 0) - (IsInputKeyDown(EKeys::A) ? 1 : 0),
		                       (IsInputKeyDown(EKeys::E) ? 1 : 0) - (IsInputKeyDown(EKeys::Q) ? 1 : 0));
		if (!bPanning)
		{
			Camera->MoveCamera(Movement, DeltaSeconds, IsInputKeyDown(EKeys::LeftShift));
		}
	}
	if (bBrushStroke)
	{
		FVector BrushGround;
		if (!IsInputKeyDown(EKeys::LeftMouseButton) || !CanUseBrush() || !GetGroundUnderCursor(BrushGround))
		{
			EndBrushStroke();
		}
		else
		{
			ContinueBrushStroke(BrushGround);
		}
	}
	if (!Mode || !Mode->GetScenario())
	{
		return;
	}
	if (Mode->GetPhase() == ELSAutoBattlePhase::Preparation)
	{
		const ALSAutoBattleScenario* Scenario = Mode->GetScenario();
		const FVector Center((Scenario->DeploymentMin.X + Scenario->DeploymentMax.X) * 0.5,
		                     (Scenario->DeploymentMin.Y + Scenario->DeploymentMax.Y) * 0.5, 8);
		DrawDebugBox(GetWorld(), Center,
		             FVector((Scenario->DeploymentMax.X - Scenario->DeploymentMin.X) * 0.5,
		                     (Scenario->DeploymentMax.Y - Scenario->DeploymentMin.Y) * 0.5, 2),
		             FColor::Cyan, false, -1, 0, 4);
		FVector Ground;
		if (!bCameraCaptured && (!Widget || !Widget->IsPointerOverPanel()) && GetGroundUnderCursor(Ground))
		{
			if (GetWorld()->GetTimeSeconds() >= NextPreviewTime)
			{
				FVector Projected;
				bPreviewValid = bCastlePlacement
				                    ? Mode->CanPlaceCastle(Ground, Projected, EditingFaction)
				                    : Mode->CanPlace(ChosenTroop, Ground, SelectionId, Projected, EditingFaction);
				NextPreviewTime = GetWorld()->GetTimeSeconds() + PreviewRefreshInterval;
			}
			DrawDebugCircle(GetWorld(), Ground + FVector(0, 0, 5),
			                bEraseBrush ? EraseRadius
			                            : (bCastlePlacement ? 142 : (ChosenTroop == ELSTroopType::Cavalry ? 65 : 42)),
			                32, bEraseBrush ? FColor::Orange : (bPreviewValid ? FColor::Green : FColor::Red), false, -1,
			                0, 2, FVector::ForwardVector, FVector::RightVector, false);
		}
	}
	if (ALSUnit* Unit = Mode->GetPlacementActor(SelectionId))
	{
		DrawDebugCircle(GetWorld(), Unit->GetActorLocation() - FVector(0, 0, 80), 80, 32, FColor::Yellow, false, -1, 0,
		                3, FVector::ForwardVector, FVector::RightVector, false);
	}
}

bool ALSAutoBattleController::GetGroundUnderCursor(FVector& OutGround) const
{
	float X, Y;
	if (!GetMousePosition(X, Y))
	{
		return false;
	}
	FVector Origin, Direction;
	if (!DeprojectScreenPositionToWorld(X, Y, Origin, Direction))
	{
		return false;
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * 100000, Objects))
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

void ALSAutoBattleController::LeftClick()
{
	if (bCameraCaptured || (Widget && Widget->IsPointerOverPanel()))
	{
		return;
	}
	ALSAutoBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	if (Mode && Mode->GetPhase() == ELSAutoBattlePhase::Fighting)
	{
		if (ALSAutoBattleCastle* Castle = Mode->GetAllyCastle(); ULSCombatTargetLibrary::IsCombatAlive(Castle))
			ConfirmCastleWeapon(*Castle);
		return;
	}
	if (!Mode || Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
	{
		return;
	}
	if (bEraseBrush ||
	    (!bCastlePlacement && !SelectionId && !IsInputKeyDown(EKeys::LeftShift) && !IsInputKeyDown(EKeys::RightShift)))
	{
		FVector Ground;
		if (GetGroundUnderCursor(Ground))
		{
			BeginBrushStroke(Ground);
		}
		return;
	}
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		const int32 Id = Mode->FindPlacementId(Hit.GetActor());
		if (Id && ((Id < 0) == (EditingFaction == ELSFaction::Enemy)))
		{
			SelectionId = Id;
			bCastlePlacement = false;
			if (const auto* Troop = Cast<ALSTroopUnit>(Hit.GetActor()))
			{
				ChosenTroop = Troop->GetTroopType();
			}
			if (Widget)
			{
				Widget->RefreshState();
			}
			return;
		}
		if (Hit.GetActor() == (EditingFaction == ELSFaction::Ally ? Mode->GetAllyCastle() : Mode->GetEnemyCastle()))
		{
			ChooseCastle();
			return;
		}
	}
	FVector Ground;
	if (!GetGroundUnderCursor(Ground))
	{
		return;
	}
	if (bCastlePlacement)
	{
		LastRequest = Mode->MoveCastle(Ground, EditingFaction);
	}
	else if (SelectionId)
	{
		LastRequest = Mode->MoveUnit(SelectionId, Ground);
	}
	else
	{
		int32 Id = 0;
		LastRequest = Mode->PlaceUnit(ChosenTroop, Ground, Id, EditingFaction);
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}

void ALSAutoBattleController::ChooseTroop(ELSTroopType Type)
{
	if (ALSTroopUnit::GetStatsRowName(Type).IsNone())
	{
		return;
	}
	EndBrushStroke();
	bEraseBrush = false;
	ChosenTroop = Type;
	SelectionId = 0;
	bCastlePlacement = false;
	LastRequest = ELSAutoBattleRequest::Success;
	NextPreviewTime = 0;
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::ChooseCastle()
{
	EndBrushStroke();
	bEraseBrush = false;
	SelectionId = 0;
	bCastlePlacement = true;
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::ChooseWeapon(ELSCastleWeapon Weapon)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		if (Mode->GetPhase() == ELSAutoBattlePhase::Fighting)
		{
			CancelCastleAim();
			auto* Castle = Mode->GetAllyCastle();
			LastWeaponResult = ULSCombatTargetLibrary::IsCombatAlive(Castle)
			    ? Castle->Weapons->GetStatus(Weapon) : ELSWeaponCastResult::Unavailable;
			if (LastWeaponResult == ELSWeaponCastResult::Ready)
				ArmedWeapon = Weapon;
		}
		else
		{
			LastRequest = Mode->SelectWeapon(Weapon, EditingFaction);
		}
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}

void ALSAutoBattleController::ArmCannon()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		ChooseWeapon(ELSCastleWeapon::Cannon);
}

void ALSAutoBattleController::ArmShotgun()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		ChooseWeapon(ELSCastleWeapon::Shotgun);
}

void ALSAutoBattleController::ArmBallista()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		ChooseWeapon(ELSCastleWeapon::Ballista);
}

void ALSAutoBattleController::ChooseField(ELSCastleField Field)
{
	auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	if (!Mode || Mode->GetPhase() != ELSAutoBattlePhase::Fighting)
		return;
	ArmedWeapon.Reset();
	ArmedField.Reset();
	LastWeaponResult = Mode->GetFieldStatus(Field);
	if (LastWeaponResult == ELSWeaponCastResult::Ready)
		ArmedField = Field;
	RefreshFieldCamera();
	if (Widget)
		Widget->RefreshState();
}

void ALSAutoBattleController::ArmSwamp()
{
	ChooseField(ELSCastleField::Swamp);
}

void ALSAutoBattleController::ArmStim()
{
	ChooseField(ELSCastleField::Stim);
}

void ALSAutoBattleController::ArmWall()
{
	ChooseField(ELSCastleField::Wall);
}

void ALSAutoBattleController::CancelCastleAim()
{
	ArmedWeapon.Reset();
	ArmedField.Reset();
	LastWeaponResult = ELSWeaponCastResult::Ready;
	RefreshFieldCamera();
}

void ALSAutoBattleController::RefreshFieldCamera()
{
	auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn());
	const auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	if (!Camera || !Mode)
		return;

	const auto* Scenario = Mode->GetScenario();
	const auto* Castle = Mode->GetAllyCastle();
	if (ArmedField.IsSet() && Mode->GetPhase() == ELSAutoBattlePhase::Fighting && Scenario && IsValid(Castle))
	{
		int32 Width = 0;
		int32 Height = 0;
		GetViewportSize(Width, Height);
		const float AspectRatio = Width > 0 && Height > 0 ? static_cast<float>(Width) / Height : 16.0f / 9.0f;
		if (!Camera->IsTacticalView())
			Camera->SetTacticalView(Scenario->BoundsMax - Scenario->BoundsMin, AspectRatio);
	}
	else if (Camera->IsTacticalView())
	{
		if (IsValid(Castle))
			Camera->FollowCastle(*Castle, 0, Mode->GetPhase() != ELSAutoBattlePhase::Fighting);
		else
			Camera->ResetView();
	}
}

void ALSAutoBattleController::ConfirmCastleWeapon(ALSAutoBattleCastle& Castle)
{
	if (!ArmedWeapon.IsSet() && !ArmedField.IsSet())
		return;
	FVector Aim;
	if (!GetGroundUnderCursor(Aim))
		LastWeaponResult = ELSWeaponCastResult::InvalidAim;
	else if (ArmedField.IsSet())
	{
		auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
		LastWeaponResult = Mode ? Mode->CastField(ArmedField.GetValue(), Aim) : ELSWeaponCastResult::Unavailable;
	}
	else
		LastWeaponResult = Castle.Weapons->CastWeapon(ArmedWeapon.GetValue(), Aim);
	if (LastWeaponResult == ELSWeaponCastResult::Ready)
		CancelCastleAim();
	if (Widget)
		Widget->RefreshState();
}

void ALSAutoBattleController::UpdateCastleAim(const ALSAutoBattleCastle& Castle) const
{
	if ((!ArmedWeapon.IsSet() && !ArmedField.IsSet()) || (Widget && Widget->IsPointerOverPanel()))
		return;
	FVector Aim;
	if (!GetGroundUnderCursor(Aim))
		return;
	if (ArmedField.IsSet())
	{
		if (const auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
		{
			const auto Field = ArmedField.GetValue();
			if (Field == ELSCastleField::Wall)
			{
				FTransform WallTransform;
				FVector Extent;
				if (Mode->GetWallPreview(Aim, WallTransform, Extent))
				{
					const FColor WallColor = Mode->ValidateFieldAim(Field, Aim) == ELSWeaponCastResult::Ready
					    ? FColor::Cyan : FColor::Red;
					DrawDebugBox(GetWorld(), WallTransform.GetLocation(), Extent, WallTransform.GetRotation(),
					             WallColor, false, -1, 0, 3);
				}
				return;
			}
			const FColor FieldColor = Field == ELSCastleField::Swamp ? FColor(160, 75, 210) : FColor::Green;
			const FColor Color = Mode->ValidateFieldAim(Field, Aim) == ELSWeaponCastResult::Ready
			    ? FieldColor : FColor::Red;
			DrawDebugCircle(GetWorld(), Aim + FVector(0, 0, 8), Mode->GetFieldRadius(), 48, Color, false, -1, 0, 3,
			                FVector::ForwardVector, FVector::RightVector, false);
		}
		return;
	}
	const ELSCastleWeapon Weapon = ArmedWeapon.GetValue();
	const FLSCastleWeaponSettings* Settings = Castle.Weapons->FindSettings(Weapon);
	if (!Settings)
		return;
	const FColor Color = Castle.Weapons->ValidateAim(Weapon, Aim) == ELSWeaponCastResult::Ready ? FColor::Cyan : FColor::Red;
	const FVector Origin = Castle.Weapons->GetWeaponFireOrigin(Weapon, Aim);
	const FVector Direction = (Aim - Origin).GetSafeNormal2D();
	if (Weapon == ELSCastleWeapon::Cannon)
	{
		DrawDebugCircle(GetWorld(), Aim + FVector(0, 0, 8), Settings->Radius, 32, Color, false, -1, 0, 3,
		                FVector::ForwardVector, FVector::RightVector, false);
	}
	else if (Weapon == ELSCastleWeapon::Shotgun)
	{
		const float HalfAngle = Settings->ConeAngle * 0.5f;
		FVector Previous = Origin;
		for (int32 Segment = 0; Segment <= 12; ++Segment)
		{
			const FVector Edge = Origin + Direction.RotateAngleAxis(-HalfAngle + Settings->ConeAngle * Segment / 12,
			                                                        FVector::UpVector) * Settings->Range;
			DrawDebugLine(GetWorld(), Previous, Edge, Color, false, -1, 0, 3);
			Previous = Edge;
		}
		DrawDebugLine(GetWorld(), Previous, Origin, Color, false, -1, 0, 3);
	}
	else
	{
		DrawDebugLine(GetWorld(), Origin, Origin + Direction * Settings->Range, Color, false, -1, 0, 4);
	}
}
void ALSAutoBattleController::BeginBattle()
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		LastRequest = Mode->StartBattle();
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::Retry()
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		LastRequest = Mode->RetryBattle();
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::ClearFormation()
{
	EndBrushStroke();
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		LastRequest = Mode->ResetFormation(EditingFaction);
	}
	SelectionId = 0;
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::ToggleEraseBrush()
{
	if (Widget && Widget->IsEditingText())
	{
		return;
	}
	const auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	if (!Mode || Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
	{
		return;
	}
	EndBrushStroke();
	bEraseBrush = !bEraseBrush;
	SelectionId = 0;
	bCastlePlacement = false;
	LastRequest = ELSAutoBattleRequest::Success;
	NextPreviewTime = 0;
	if (Widget)
	{
		Widget->RefreshState();
	}
}

bool ALSAutoBattleController::CanUseBrush() const
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>() : nullptr;
	return Mode && Mode->GetScenario() && Mode->GetPhase() == ELSAutoBattlePhase::Preparation && !bCameraCaptured &&
	       !bCastlePlacement && !SelectionId && (!Widget || !Widget->IsPointerOverPanel()) &&
	       FMath::IsFinite(BrushSpacing) && BrushSpacing >= 140 && FMath::IsFinite(EraseRadius) && EraseRadius > 0;
}

void ALSAutoBattleController::BeginBrushStroke(const FVector& Ground)
{
	EndBrushStroke();
	if (!CanUseBrush() || Ground.ContainsNaN())
	{
		return;
	}
	bBrushStroke = true;
	PreviousBrushGround = Ground;
	if (bEraseBrush)
	{
		EraseBrushSegment(Ground, Ground);
	}
	else
	{
		PaintBrushSample(Ground);
	}
}

void ALSAutoBattleController::ContinueBrushStroke(const FVector& Ground)
{
	if (!bBrushStroke)
	{
		return;
	}
	if (!CanUseBrush() || Ground.ContainsNaN())
	{
		EndBrushStroke();
		return;
	}
	if (bEraseBrush)
	{
		EraseBrushSegment(PreviousBrushGround, Ground);
	}
	else
	{
		const FVector Delta = Ground - PreviousBrushGround;
		const double Distance = Delta.Size2D();
		// 프레임별 끝점마다 생성하지 않고 누적 이동 거리를 사용한다. 정지·프레임률에 따라 밀도가 달라지지 않는다.
		double Along = BrushSpacing - BrushTravel;
		for (; Along <= Distance; Along += BrushSpacing)
		{
			PaintBrushSample(PreviousBrushGround + Delta * (Along / Distance));
			if (!bBrushStroke)
			{
				return;
			}
		}
		BrushTravel = FMath::Fmod(BrushTravel + Distance, static_cast<double>(BrushSpacing));
	}
	PreviousBrushGround = Ground;
}

void ALSAutoBattleController::EndBrushStroke()
{
	bBrushStroke = false;
	BrushTravel = 0;
}

void ALSAutoBattleController::PaintBrushSample(const FVector& Ground)
{
	auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	int32 Id = 0;
	LastRequest = Mode->PlaceUnit(ChosenTroop, Ground, Id, EditingFaction);
	// 겹침·장애물은 건너뛰고 편집 불가 상태에서만 스트로크를 종료한다.
	if (LastRequest == ELSAutoBattleRequest::PopulationFull || LastRequest == ELSAutoBattleRequest::WrongPhase ||
	    LastRequest == ELSAutoBattleRequest::Unavailable || LastRequest == ELSAutoBattleRequest::InvalidData)
	{
		EndBrushStroke();
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}

void ALSAutoBattleController::EraseBrushSegment(const FVector& Start, const FVector& End)
{
	auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>();
	const auto& Formation =
	    EditingFaction == ELSFaction::Enemy ? Mode->GetScenario()->EnemyFormation : Mode->GetFormation();
	TArray<int32> RemoveIds;
	const FVector A(Start.X, Start.Y, 0), B(End.X, End.Y, 0);
	for (const FLSAutoBattlePlacement& Placement : Formation)
	{
		const ALSUnit* Unit = Mode->GetPlacementActor(Placement.Id);
		if (!Unit)
		{
			continue;
		}
		float Radius, Height;
		Unit->GetSimpleCollisionCylinder(Radius, Height);
		const FVector Center(Unit->GetActorLocation().X, Unit->GetActorLocation().Y, 0);
		if (FVector::DistSquared(Center, FMath::ClosestPointOnSegment(Center, A, B)) <=
		    FMath::Square(EraseRadius + Radius))
		{
			RemoveIds.Add(Placement.Id);
		}
	}
	// RemoveUnit이 편성 배열을 변경하므로 순회와 실제 삭제를 분리한다. 성은 편성 목록에 포함되지 않는다.
	for (int32 Id : RemoveIds)
	{
		LastRequest = Mode->RemoveUnit(Id);
		if (!bBrushStroke)
		{
			break;
		}
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}

void ALSAutoBattleController::LookPressed()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
	{
		CancelCastleAim();
		return;
	}
	if (!Widget || !Widget->IsPointerOverPanel())
	{
		bPanning = false;
		SetCameraCapture(true);
	}
}
void ALSAutoBattleController::LookReleased()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (IsInputKeyDown(EKeys::MiddleMouseButton))
	{
		bPanning = true;
	}
	else
	{
		SetCameraCapture(false);
	}
}
void ALSAutoBattleController::PanPressed()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (!Widget || !Widget->IsPointerOverPanel())
	{
		bPanning = true;
		SetCameraCapture(true);
	}
}
void ALSAutoBattleController::PanReleased()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (IsInputKeyDown(EKeys::RightMouseButton))
	{
		bPanning = false;
	}
	else
	{
		SetCameraCapture(false);
	}
}
void ALSAutoBattleController::Escape()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
	{
		CancelCastleAim();
		return;
	}
	EndBrushStroke();
	bEraseBrush = false;
	if (bCameraCaptured)
	{
		SetCameraCapture(false);
	}
	else
	{
		SelectionId = 0;
		bCastlePlacement = false;
		if (Widget)
		{
			Widget->RefreshState();
		}
	}
}
void ALSAutoBattleController::Home()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn()))
	{
		Camera->ResetView();
	}
}
void ALSAutoBattleController::WheelUp()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn()))
	{
		if (bCameraCaptured && !bPanning)
		{
			Camera->ChangeSpeed(1);
		}
		else if (!Widget || !Widget->IsPointerOverPanel())
		{
			Camera->Dolly(1);
		}
	}
}
void ALSAutoBattleController::WheelDown()
{
	if (LastPhase == ELSAutoBattlePhase::Fighting)
		return;
	if (auto* Camera = Cast<ALSFreeCameraPawn>(GetPawn()))
	{
		if (bCameraCaptured && !bPanning)
		{
			Camera->ChangeSpeed(-1);
		}
		else if (!Widget || !Widget->IsPointerOverPanel())
		{
			Camera->Dolly(-1);
		}
	}
}
void ALSAutoBattleController::EndPlay(const EEndPlayReason::Type Reason)
{
	EndBrushStroke();
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		Mode->OnChanged.RemoveDynamic(this, &ThisClass::OnBattleChanged);
	}
	if (Widget)
	{
		Widget->RemoveFromParent();
	}
	Super::EndPlay(Reason);
}

void ALSAutoBattleController::SetEditingFaction(ELSFaction Faction)
{
	if (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy)
	{
		return;
	}
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		if (Mode->GetPhase() != ELSAutoBattlePhase::Preparation)
		{
			return;
		}
	}
	EndBrushStroke();
	bEraseBrush = false;
	EditingFaction = Faction;
	SelectionId = 0;
	bCastlePlacement = false;
	NextPreviewTime = 0;
	LastRequest = ELSAutoBattleRequest::Success;
	EditorMessage.Reset();
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::SavePreset(const FString& Name, bool bOverwrite)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		if (Mode->SavePreset(Name, bOverwrite, EditorMessage))
		{
			EditorMessage = TEXT("저장 완료: ") + Name;
		}
	}
	if (Widget)
	{
		Widget->RefreshPresets();
		if (EditorMessage.StartsWith(TEXT("저장 완료")))
		{
			Widget->SelectPreset(Name);
		}
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::LoadPreset(const FString& Name)
{
	EndBrushStroke();
	if (auto* Mode = GetWorld()->GetAuthGameMode<ALSAutoBattleGameMode>())
	{
		if (Mode->LoadPreset(Name, EditorMessage))
		{
			SelectionId = 0;
			bCastlePlacement = false;
			LastRequest = ELSAutoBattleRequest::Success;
			EditorMessage = Mode->GetPhase() == ELSAutoBattlePhase::Loading
			    ? TEXT("전장 불러오는 중: ") + Name : TEXT("불러오기 완료: ") + Name;
			if (Widget)
			{
				Widget->SelectPreset(Name);
			}
		}
	}
	if (Widget)
	{
		Widget->RefreshState();
	}
}
void ALSAutoBattleController::StartHotkey()
{
	if (Widget && Widget->IsEditingText())
	{
		return;
	}
	BeginBattle();
}
