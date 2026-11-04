#include "Controls/LSCommandCardComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Castle/LSCastleCommandProviderComponent.h"
#include "Match/LSBattleOutcomePolicy.h"

TArray<AActor*> ULSCommandCardComponent::GetActiveActors()
{
	TArray<AActor*> Result;
	auto* PC = Cast<ALSPlayerController>(GetOwner());
	if (!PC)
		return Result;
	TArray<AActor*> Controllable;
	for (AActor* Actor : PC->GetSelectedActors())
	{
		const auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
		if (Selection && Selection->CanPlayerControl())
			Controllable.Add(Actor);
	}
	if (!Controllable.ContainsByPredicate(
	        [this](AActor* Actor)
	        { return Actor->FindComponentByClass<ULSSelectableComponent>()->GetSelectionType() == ActiveType; }))
		ActiveType = Controllable.IsEmpty()
		                 ? NAME_None
		                 : Controllable[0]->FindComponentByClass<ULSSelectableComponent>()->GetSelectionType();
	for (AActor* Actor : Controllable)
		if (Actor->FindComponentByClass<ULSSelectableComponent>()->GetSelectionType() == ActiveType)
			Result.Add(Actor);
	return Result;
}

TArray<FLSCommandCell> ULSCommandCardComponent::BuildCard()
{
	TArray<FLSCommandCell> Cells;
	Cells.SetNum(CellCount);
	auto* PC = Cast<ALSPlayerController>(GetOwner());
	const TArray<AActor*> Active = GetActiveActors();
	if (!PC || Active.IsEmpty())
		return Cells;
	const TCHAR* Names[] = {TEXT("이동"), TEXT("정지"), TEXT("공격"), TEXT("위치 사수")};
	const FKey Keys[] = {EKeys::M, EKeys::S, EKeys::A, EKeys::H};
	const ELSOrderKind Kinds[] = {ELSOrderKind::Move, ELSOrderKind::Idle, ELSOrderKind::AttackMove, ELSOrderKind::Hold};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto& Entry = Cells[Index].Entry;
		Entry.Id = FName(*FString::Printf(TEXT("Common%d"), Index));
		Entry.Title = FText::FromString(Names[Index]);
		Entry.Hotkey = Keys[Index];
		Entry.Cell = Index;
		for (AActor* Actor : PC->GetSelectedActors())
			if (const auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
			    Selection && Selection->CanPlayerControl())
				if (const auto* Orders = Actor->FindComponentByClass<ULSOrderComponent>())
					Entry.bEnabled |= Orders->CanExecuteOrderKind(Kinds[Index]);
	}
	TSet<int32> Conflicts;
	for (AActor* Actor : Active)
	{
		if (!IsValid(Actor))
			continue;
		TArray<ULSCommandProviderComponent*> Providers;
		Actor->GetComponents(Providers);
		for (auto* Provider : Providers)
		{
			if (!IsValid(Actor) || !IsValid(Provider))
				continue;
			const TArray<FLSCommandEntry> Entries = Provider->GetCommands(PC);
			if (!IsValid(Actor) || !IsValid(Provider))
				continue;
			const auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
			if (!Selection || !Selection->CanPlayerControl() || !PC->GetSelectedActors().Contains(Actor))
				continue;
			TSet<int32> ProviderCells;
			TSet<FName> ProviderIds;
			for (const auto& Entry : Entries)
			{
				if (Entry.Cell < 5 || Entry.Cell >= CellCount || Entry.Id.IsNone() || Entry.Title.IsEmpty() ||
				    !FMath::IsFinite(Entry.Remaining) || !FMath::IsFinite(Entry.Duration) || Entry.Remaining < 0 ||
				    Entry.Duration < 0)
					continue;
				if (ProviderCells.Contains(Entry.Cell) || ProviderIds.Contains(Entry.Id))
				{
					Conflicts.Add(Entry.Cell);
					for (int32 Previous = 5; Previous < CellCount; ++Previous)
						if (Cells[Previous].Entry.Id == Entry.Id)
							Conflicts.Add(Previous);
				}
				ProviderCells.Add(Entry.Cell);
				ProviderIds.Add(Entry.Id);
				for (int32 Previous = 5; Previous < CellCount; ++Previous)
					if (Previous != Entry.Cell && Cells[Previous].Entry.Id == Entry.Id)
					{
						Conflicts.Add(Previous);
						Conflicts.Add(Entry.Cell);
					}
				auto& Cell = Cells[Entry.Cell];
				if (Cell.Entry.Id.IsNone())
					Cell = {Entry, Provider, Actor};
				else if (Cell.Entry.Id != Entry.Id || Cell.Entry.Hotkey != Entry.Hotkey || !Cell.Provider.IsValid() ||
				         Cell.Provider->GetClass() != Provider->GetClass() ||
				         (Cell.Provider.Get() != Provider && Cell.Source.Get() == Actor))
					Conflicts.Add(Entry.Cell);
				else if (!Cell.Entry.bEnabled && Entry.bEnabled)
					Cell = {Entry, Provider, Actor};
			}
		}
	}
	// 중복 슬롯·예약 단축키는 잘못된 스킬을 실행하는 대신 차단한다.
	for (int32 Index = 5; Index < CellCount; ++Index)
		if (Cells[Index].Entry.Hotkey.IsValid())
		{
			const FKey Key = Cells[Index].Entry.Hotkey;
			const bool bCastleWeaponKey = Key == EKeys::Q || Key == EKeys::W || Key == EKeys::E;
			const int32 WeaponIndex = Key == EKeys::Q ? 0 : Key == EKeys::W ? 1 : 2;
			const bool bOwnCastleWeapon = bCastleWeaponKey &&
				Cells[Index].Provider.IsValid() && Cells[Index].Provider->IsA<ULSCastleCommandProviderComponent>() &&
				Index == 5 + WeaponIndex &&
				Cells[Index].Entry.Id == FName(*FString::Printf(TEXT("CastleWeapon%d"), WeaponIndex));
			if (bCastleWeaponKey && !bOwnCastleWeapon)
				Conflicts.Add(Index);
			if (Key == EKeys::Tab || Key == EKeys::Escape || Key == EKeys::Home || Key.IsModifierKey() ||
			    Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down ||
			    Key.IsMouseButton() || Key.IsGamepadKey() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() ||
			    Key == EKeys::Zero || Key == EKeys::One || Key == EKeys::Two || Key == EKeys::Three ||
			    Key == EKeys::Four || Key == EKeys::Five || Key == EKeys::Six || Key == EKeys::Seven ||
			    Key == EKeys::Eight || Key == EKeys::Nine)
				Conflicts.Add(Index);
			for (int32 Other = 0; Other < Index; ++Other)
				if (Cells[Other].Entry.Hotkey == Key)
				{
					Conflicts.Add(Index);
					if (Other >= 5)
						Conflicts.Add(Other);
				}
		}
	for (int32 Index : Conflicts)
	{
		Cells[Index].Entry.bEnabled = false;
		// 잘못 지정된 Esc·부대 번호가 원래 입력까지 가로채지 않게 한다.
		Cells[Index].Entry.Hotkey = FKey();
		Cells[Index].Entry.Detail = FText::FromString(TEXT("설정 충돌"));
	}
	for (int32 Index = 5; Index < CellCount; ++Index)
		if (!Cells[Index].Entry.Id.IsNone())
		{
			const auto* Selection = Cells[Index].Source.IsValid()
			                            ? Cells[Index].Source->FindComponentByClass<ULSSelectableComponent>()
			                            : nullptr;
			if (!Cells[Index].Provider.IsValid() || !Selection || !Selection->CanPlayerControl() ||
			    Selection->GetSelectionType() != ActiveType ||
			    !PC->GetSelectedActors().Contains(Cells[Index].Source.Get()))
				Cells[Index] = {};
		}
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		for (auto& Cell : Cells)
			if (!Cell.Entry.Id.IsNone())
			{
				Cell.Entry.bEnabled = false;
				Cell.Entry.Detail = FText::FromString(TEXT("전투 종료"));
				Cell.Entry.Tooltip = FText::GetEmpty();
				Cell.Entry.Remaining = 0;
				Cell.Entry.Duration = 0;
			}
	return Cells;
}

bool ULSCommandCardComponent::ExecuteCell(int32 Index, const FLSCommandCell& Expected)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	const FLSCommandCell Snapshot = Expected;
	const auto Cells = BuildCard();
	if (!Cells.IsValidIndex(Index))
		return false;
	const auto& Cell = Cells[Index];
	if (!Cell.Entry.bEnabled || Cell.Entry.Id != Snapshot.Entry.Id || Cell.Source != Snapshot.Source ||
	    Cell.Provider != Snapshot.Provider)
		return false;
	auto* PC = Cast<ALSPlayerController>(GetOwner());
	switch (Index)
	{
	case 0:
		PC->ArmMove();
		return PC->GetPendingInput() == ELSCommandInput::Move;
	case 1:
		PC->StopSelected();
		return true;
	case 2:
		PC->ArmAttack();
		return PC->GetPendingInput() == ELSCommandInput::Attack;
	case 3:
		PC->HoldSelected();
		return true;
	default:
		return !LSBattleOutcome::AreActionsStopped(GetWorld()) && Cell.Provider.IsValid() &&
		       Cell.Provider->ExecuteCommand(Cell.Entry.Id, PC);
	}
}

bool ULSCommandCardComponent::HandleHotkey(FKey Key)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	const auto Cells = BuildCard();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
		if (!Cells[Index].Entry.Id.IsNone() && Cells[Index].Entry.Hotkey == Key)
		{
			ExecuteCell(Index, Cells[Index]);
			return true;
		}
	return false;
}

void ULSCommandCardComponent::CycleGroup()
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return;
	GetActiveActors();
	auto* PC = Cast<ALSPlayerController>(GetOwner());
	if (!PC)
		return;
	TArray<FName> Types;
	for (AActor* Actor : PC->GetSelectedActors())
		if (auto* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
		    Selection && Selection->CanPlayerControl())
			Types.AddUnique(Selection->GetSelectionType());
	if (!Types.IsEmpty())
	{
		ActiveType = Types[(Types.IndexOfByKey(ActiveType) + 1) % Types.Num()];
		PC->CancelInput();
	}
}

FText ULSCommandCardComponent::GetGroupText()
{
	const auto Actors = GetActiveActors();
	if (Actors.IsEmpty())
		return FText::FromString(TEXT("명령 · 조작 대상 없음"));
	const auto* Selection = Actors[0]->FindComponentByClass<ULSSelectableComponent>();
	const FText Name =
	    Selection->CommandGroupName.IsEmpty() ? FText::FromName(ActiveType) : Selection->CommandGroupName;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return FText::FromString(FString::Printf(TEXT("%s %d"), *Name.ToString(), Actors.Num()));
	return FText::FromString(FString::Printf(TEXT("%s %d  ·  Tab 병종 전환"), *Name.ToString(), Actors.Num()));
}
