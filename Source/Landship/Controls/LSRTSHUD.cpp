#include "Controls/LSRTSHUD.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackParticipant.h"
#include "Engine/Canvas.h"

FBox2D ALSRTSHUD::InfoPanel() const
{
	int32 Width = 0, Height = 0;
	if (PlayerOwner) { PlayerOwner->GetViewportSize(Width, Height); }
	return FBox2D(FVector2D(0, FMath::Max(0, Height - 125)), FVector2D(FMath::Min(340, Width), Height));
}

FBox2D ALSRTSHUD::CommandPanel() const
{
	int32 Width = 0, Height = 0;
	if (PlayerOwner) { PlayerOwner->GetViewportSize(Width, Height); }
	return FBox2D(FVector2D(FMath::Max(0, Width - 300), FMath::Max(0, Height - 125)), FVector2D(Width, Height));
}

FBox2D ALSRTSHUD::CommandButton(int32 Index) const
{
	const FVector2D Min = CommandPanel().Min + FVector2D(12 + Index * 70, 55);
	return FBox2D(Min, Min + FVector2D(62, 46));
}

bool ALSRTSHUD::IsOverPanel(const FVector2D& Mouse) const
{
	return InfoPanel().IsInsideOrOn(Mouse) || CommandPanel().IsInsideOrOn(Mouse);
}

bool ALSRTSHUD::CanUseButton(int32 Index) const
{
	const ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (!Controller) { return false; }
	for (AActor* Actor : Controller->GetSelectedActors())
	{
		const auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
		const auto* Orders = Actor->FindComponentByClass<ULSOrderComponent>();
		if (Selection && Selection->CanPlayerControl() && Orders
			&& (Index >= 2 || (Index == 0 && Orders->CanMove()) || (Index == 1 && Orders->CanAttack()))) { return true; }
	}
	return false;
}

bool ALSRTSHUD::HandleClick(const FVector2D& Mouse)
{
	if (!IsOverPanel(Mouse)) { return false; }
	ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (!Controller) { return true; }
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!CommandButton(Index).IsInsideOrOn(Mouse) || !CanUseButton(Index)) { continue; }
		if (Index < 2) { Controller->SetPendingInput(Index == 0 ? ELSCommandInput::Move : ELSCommandInput::Attack); }
		else
		{
			Controller->CancelInput();
			Controller->IssueSelectionOrder(Index == 2 ? ELSOrderKind::Idle : ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false);
		}
	}
	return true;
}

void ALSRTSHUD::DrawHUD()
{
	Super::DrawHUD();
	ALSPlayerController* Controller = Cast<ALSPlayerController>(PlayerOwner);
	if (!Controller || !Canvas) { return; }
	FVector2D Start, End;
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
	const TArray<AActor*> Selection = Controller->GetSelectedActors();
	for (AActor* Actor : Selection)
	{
		const bool bControlled = Actor->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		float Radius, HalfHeight;
		Actor->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const FVector Base = Actor->GetActorLocation() - FVector(0, 0, HalfHeight - 5);
		// 선택 표시는 Shipping에서도 필요하므로 디버그 드로잉에 의존하지 않는다.
		const float RingRadius = FMath::Max(Radius, 25.0f) + 8;
		for (int32 Segment = 0; Segment < 40; ++Segment)
		{
			const float AngleA = Segment * 2 * PI / 40;
			const float AngleB = (Segment + 1) * 2 * PI / 40;
			FVector2D A, B;
			if (Controller->ProjectWorldLocationToScreen(Base + FVector(FMath::Cos(AngleA), FMath::Sin(AngleA), 0) * RingRadius, A)
				&& Controller->ProjectWorldLocationToScreen(Base + FVector(FMath::Cos(AngleB), FMath::Sin(AngleB), 0) * RingRadius, B))
			{
				DrawLine(A.X, A.Y, B.X, B.Y, bControlled ? FLinearColor::Green : FLinearColor::Red, 2);
			}
		}
		float Current = 0, Max = 0;
		FVector2D Screen;
		if (Actor->Implements<ULSBasicAttackParticipant>()
			&& ILSBasicAttackParticipant::Execute_GetCombatHealth(Actor, Current, Max) && Max > 0
			&& Controller->ProjectWorldLocationToScreen(Actor->GetActorLocation() + FVector(0, 0, HalfHeight + 20), Screen))
		{
			DrawRect(FLinearColor::Black, Screen.X - 30, Screen.Y, 60, 6);
			DrawRect(bControlled ? FLinearColor::Green : FLinearColor::Red, Screen.X - 29, Screen.Y + 1, 58 * FMath::Clamp(Current / Max, 0.0f, 1.0f), 4);
		}
	}
	for (const FBox2D& Box : { InfoPanel(), CommandPanel() })
	{
		DrawRect(FLinearColor(0.025f, 0.04f, 0.07f, 0.92f), Box.Min.X, Box.Min.Y, Box.GetSize().X, Box.GetSize().Y);
	}
	const FVector2D Info = InfoPanel().Min + FVector2D(12, 10);
	DrawText(FString::Printf(TEXT("Selected: %d | Home: focus"), Selection.Num()), FLinearColor::White, Info.X, Info.Y);
	if (!Selection.IsEmpty())
	{
		AActor* First = Selection[0];
		const bool bControlled = First->FindComponentByClass<ULSSelectableComponent>()->CanPlayerControl();
		DrawText(First->GetName() + (bControlled ? TEXT(" [CONTROL]") : TEXT(" [INSPECT ONLY]")), FLinearColor::White, Info.X, Info.Y + 22);
		float Current = 0, Max = 0;
		const bool bHealth = First->Implements<ULSBasicAttackParticipant>() && ILSBasicAttackParticipant::Execute_GetCombatHealth(First, Current, Max);
		DrawText(bHealth ? FString::Printf(TEXT("HP %.0f / %.0f"), Current, Max) : TEXT("HP: -- (not connected)"), FLinearColor::White, Info.X, Info.Y + 44);
		if (const ULSOrderComponent* Orders = First->FindComponentByClass<ULSOrderComponent>())
		{
			DrawText(FString::Printf(TEXT("Order: %s | Queued: %d"), *UEnum::GetValueAsString(Orders->GetOrderKind()), Orders->GetQueuedCount()), FLinearColor::White, Info.X, Info.Y + 66);
		}
	}
	const FVector2D Commands = CommandPanel().Min + FVector2D(12, 10);
	const ELSCommandInput Pending = Controller->GetPendingInput();
	DrawText(Pending == ELSCommandInput::None ? TEXT("Right click: move / attack") : (Pending == ELSCommandInput::Attack ? TEXT("ATTACK: choose target | Esc: cancel") : TEXT("MOVE: choose ground | Esc: cancel")),
		Pending == ELSCommandInput::None ? FLinearColor::White : FLinearColor::Yellow, Commands.X, Commands.Y);
	DrawText(TEXT("Shift: queue | Ctrl+0..9: assign"), FLinearColor::White, Commands.X, Commands.Y + 20);
	const TCHAR* Labels[] = { TEXT("M\nMove"), TEXT("A\nAttack"), TEXT("S\nStop"), TEXT("H\nHold") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FBox2D Button = CommandButton(Index);
		const bool bEnabled = CanUseButton(Index);
		DrawRect(bEnabled ? FLinearColor(0.12f, 0.25f, 0.35f) : FLinearColor(0.08f, 0.09f, 0.1f), Button.Min.X, Button.Min.Y, Button.GetSize().X, Button.GetSize().Y);
		DrawText(Labels[Index], bEnabled ? FLinearColor::White : FLinearColor::Gray, Button.Min.X + 8, Button.Min.Y + 5);
	}
}
