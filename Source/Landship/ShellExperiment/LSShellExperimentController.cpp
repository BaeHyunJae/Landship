#include "LSShellExperimentController.h"

#include "Castle/LSCastle.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Enemies/LSOutpost.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "LSShellExperimentCoordinator.h"
#include "LSShellExperimentEnemy.h"
#include "LSShellExperimentWidget.h"
#include "LSShellReactiveComponent.h"
#include "LSShellExplosive.h"
#include "LSShellWeakPoint.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

ALSShellExperimentController::ALSShellExperimentController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
}

void ALSShellExperimentController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	if (WidgetClass)
	{
		ExperimentWidget = CreateWidget<UUserWidget>(this, WidgetClass);
		if (ExperimentWidget) ExperimentWidget->AddToViewport();
	}
	if (GetPawn()) SetViewTarget(GetPawn());
}

void ALSShellExperimentController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalController() && InPawn) SetViewTarget(InPawn);
}

void ALSShellExperimentController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ThisClass::Fire);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ThisClass::ToggleCompose);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ThisClass::SwapModules);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ThisClass::BeginDrag);
	InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &ThisClass::EndDrag);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ThisClass::ZoomIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ThisClass::ZoomOut);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ThisClass::TriggerPending);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ThisClass::RecallMaterial);
}

void ALSShellExperimentController::RecallMaterial()
{
	if (!CanUseWorldPointer()) return;
	if (auto* Coordinator = GetCoordinator(); Coordinator && !Coordinator->IsComposing())
	{
		FHitResult Hit;
		if (TraceAim(Hit)) Coordinator->RecallGeneratedMaterial(Hit.GetActor());
	}
}

ALSShellExperimentCoordinator* ALSShellExperimentController::GetCoordinator() const
{
	return ALSShellExperimentCoordinator::Find(GetWorld());
}

bool ALSShellExperimentController::HasGameFocus() const
{
	const UGameViewportClient* Client = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	return IsLocalController() && Client && Client->Viewport && Client->Viewport->HasFocus() &&
		Client->Viewport->IsForegroundWindow();
}

bool ALSShellExperimentController::CanUseWorldPointer() const
{
	if (!HasGameFocus() || !FSlateApplication::IsInitialized()) return false;
	const UGameViewportClient* Client = GetWorld()->GetGameViewport();
	const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(
		FSlateApplication::Get().GetCursorPos(), FSlateApplication::Get().GetInteractiveTopLevelWindows());
	const TSharedPtr<SViewport> Viewport = Client->GetGameViewportWidget();
	if (!Path.IsValid() || !Viewport.IsValid() || Path.Widgets.Last().Widget != Viewport) return false;
	float X, Y;
	int32 Width, Height;
	GetViewportSize(Width, Height);
	return GetMousePosition(X, Y) && X >= 0 && Y >= 0 && X < Width && Y < Height;
}

bool ALSShellExperimentController::TraceAim(FHitResult& OutHit) const
{
	FVector Origin, Direction;
	if (!DeprojectMousePositionToWorld(Origin, Direction)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellExperimentAim), false);
	if (const ALSShellExperimentCoordinator* Coordinator = GetCoordinator())
	{
		if (ALSCastle* Castle = Coordinator->PlayerCastle)
		{
			Query.AddIgnoredActor(Castle);
			TArray<AActor*> Attached;
			Castle->GetAttachedActors(Attached, true, true);
			Query.AddIgnoredActors(Attached);
		}
	}
	while (GetWorld()->LineTraceSingleByChannel(OutHit, Origin, Origin + Direction * 100000,
		ECC_Visibility, Query))
	{
		AActor* Actor = OutHit.GetActor();
		UPrimitiveComponent* Body = OutHit.GetComponent();
		if (!Body) return false;
		if (ULSHealthComponent* Health = Actor ? Actor->FindComponentByClass<ULSHealthComponent>() : nullptr)
		{
			FLSHealthSnapshot Snapshot;
			const auto* Reactive = Actor->FindComponentByClass<ULSShellReactiveComponent>();
			const bool bPhysicalBody = GetCoordinator() && GetCoordinator()->IsSupplyExperiment() && Reactive && Reactive->IsPhysicalBodyPresent();
			if ((!Health->TryGetHealth(Snapshot) || Snapshot.bIsDead) && !bPhysicalBody)
			{
				Query.AddIgnoredActor(Actor);
				continue;
			}
		}
		if (Body->GetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile) != ECR_Block)
		{
			Query.AddIgnoredComponent(Body);
			continue;
		}
		return true;
	}
	return false;
}

bool ALSShellExperimentController::GetAimPoint(FVector& OutPoint) const
{
	if (!bHasAim) return false;
	OutPoint = AimPoint;
	return true;
}

FString ALSShellExperimentController::GetAimHint() const
{
	const AActor* Target = AimTarget.Get();
	if (!bHasAim || !Target) return FString();
	if (const ALSShellWeakPoint* WeakPoint = Cast<ALSShellWeakPoint>(Target))
	{
		FLSHealthSnapshot Health;
		WeakPoint->Health->TryGetHealth(Health);
		return FString::Printf(TEXT("동력로 약점 · HP %.0f · 파괴 시 대형 기폭·분열 해금"), Health.CurrentHealth);
	}
	if (const ALSShellExperimentCoordinator* Coordinator = GetCoordinator())
		if (Target == Coordinator->CoreOutpost.Get())
			return TEXT("중앙 동력로 · 직접 피해 감소 · 2차 폭발에 취약");
	if (const ALSShellExplosive* Explosive = Cast<ALSShellExplosive>(Target))
		return Explosive->bAuxiliaryFacility
			? TEXT("보조 시설 · 중력으로 움직이지 않는다 · 파괴 시 기폭")
			: TEXT("고정 폭발물 · 중력으로 움직이지 않는다");
	if (const ALSShellExperimentEnemy* Enemy = Cast<ALSShellExperimentEnemy>(Target))
	{
		if (Enemy->EnemyKind == ELSShellEnemyKind::Carrier)
			return TEXT("운반차 · 중력으로 이동 · 충돌/파괴 시 기폭");
		if (Enemy->EnemyKind == ELSShellEnemyKind::Assault)
			return TEXT("돌격병 · 중력으로 이동");
		if (Enemy->IsHeavy()) return TEXT("중장갑 병기 · 위쪽 청록색 구체가 약점 · 격파 시 분열 해금");
	}
	return FString();
}

bool ALSShellExperimentController::CanEditComposition() const
{
	const ALSShellExperimentCoordinator* Coordinator = GetCoordinator();
	return HasGameFocus() && Coordinator && Coordinator->IsBattleActive() && Coordinator->IsComposing();
}

void ALSShellExperimentController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	ALSShellExperimentCoordinator* Coordinator = GetCoordinator();
	if (!HasGameFocus() || !Coordinator || !Coordinator->IsBattleActive())
	{
		CancelCompose();
		EndDrag();
		bHasAim = false;
		AimTarget.Reset();
		return;
	}
	if (bDragging && (!IsInputKeyDown(EKeys::MiddleMouseButton) || !CanUseWorldPointer())) EndDrag();
	FHitResult Hit;
	bHasAim = CanUseWorldPointer() && TraceAim(Hit);
	AimTarget = bHasAim ? Hit.GetActor() : nullptr;
	if (bHasAim)
	{
		AimPoint = Hit.ImpactPoint;
		Coordinator->SetAimPoint(AimPoint);
	}
	float X, Y;
	if (bDragging && CanUseWorldPointer() && GetMousePosition(X, Y))
	{
		const FVector2D Current(X, Y);
		if (ALSRTSCameraPawn* Camera = Cast<ALSRTSCameraPawn>(GetPawn())) Camera->Drag(Current - LastMousePosition);
		LastMousePosition = Current;
	}
}

void ALSShellExperimentController::Fire()
{
	ALSShellExperimentCoordinator* Coordinator = GetCoordinator();
	FHitResult Hit;
	if (Coordinator && Coordinator->IsBattleActive() && !Coordinator->IsComposing() &&
		CanUseWorldPointer() && TraceAim(Hit)) Coordinator->FireAt(Hit.ImpactPoint);
}

void ALSShellExperimentController::ToggleCompose()
{
	if (ALSShellExperimentCoordinator* Coordinator = GetCoordinator())
	{
		if (!Coordinator->IsBattleActive() || !HasGameFocus()) return;
		if (Coordinator->IsComposing()) EndCompose();
		else Coordinator->SetComposing(true);
	}
}

void ALSShellExperimentController::EndCompose()
{
	if (!HasGameFocus())
	{
		CancelCompose();
		return;
	}
	if (ALSShellExperimentCoordinator* Coordinator = GetCoordinator()) Coordinator->SetComposing(false);
}

void ALSShellExperimentController::CancelCompose()
{
	if (ALSShellExperimentCoordinator* Coordinator = GetCoordinator()) Coordinator->CancelCompose();
}

void ALSShellExperimentController::SwapModules()
{
	if (ALSShellExperimentCoordinator* Coordinator = GetCoordinator())
		if (CanEditComposition()) Coordinator->SwapModules();
}

void ALSShellExperimentController::BeginDrag()
{
	float X, Y;
	const ALSShellExperimentCoordinator* Coordinator = GetCoordinator();
	if (Coordinator && Coordinator->IsBattleActive() && CanUseWorldPointer() && GetMousePosition(X, Y))
	{
		bDragging = true;
		LastMousePosition = FVector2D(X, Y);
	}
}

void ALSShellExperimentController::EndDrag() { bDragging = false; }
void ALSShellExperimentController::ZoomIn() { Zoom(1); }
void ALSShellExperimentController::ZoomOut() { Zoom(-1); }

void ALSShellExperimentController::Zoom(float Direction)
{
	const ALSShellExperimentCoordinator* Coordinator = GetCoordinator();
	if (Coordinator && Coordinator->IsBattleActive() && CanUseWorldPointer())
		if (ALSRTSCameraPawn* Camera = Cast<ALSRTSCameraPawn>(GetPawn())) Camera->Zoom(Direction);
}

void ALSShellExperimentController::TriggerPending()
{
	if (ALSShellExperimentCoordinator* Coordinator = GetCoordinator())
		if (HasGameFocus() && Coordinator->IsBattleActive() && Coordinator->CanManualTrigger()) Coordinator->TriggerPending();
}

void ALSShellExperimentController::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelCompose();
	if (ExperimentWidget) ExperimentWidget->RemoveFromParent();
	Super::EndPlay(Reason);
}
