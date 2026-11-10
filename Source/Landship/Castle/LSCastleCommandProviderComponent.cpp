#include "Castle/LSCastleCommandProviderComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Controls/LSPlayerController.h"
#include "Match/LSBattleOutcomePolicy.h"

TArray<FLSCommandEntry> ULSCastleCommandProviderComponent::GetCommands_Implementation(ALSPlayerController* PC) const
{
	TArray<FLSCommandEntry> Result;
	const auto* Castle = Cast<ALSCastle>(GetOwner());
	if (!PC || !Castle || PC->GetSelectedArmedCastle() != Castle)
		return Result;
	const TCHAR* WeaponNames[] = {TEXT("일반 대포"), TEXT("산탄"), TEXT("발리스타")};
	const TCHAR* TroopNames[] = {TEXT("창병"), TEXT("궁병"), TEXT("기마병"), TEXT("의무병"),
	    TEXT("방패병"), TEXT("중화기병"), TEXT("자폭병")};
	const FKey WeaponKeys[] = {
	    PC->UsesDirectCastleControl() ? EKeys::One : EKeys::Q,
	    PC->UsesDirectCastleControl() ? EKeys::Two : EKeys::W,
	    PC->UsesDirectCastleControl() ? EKeys::Three : EKeys::E};
	const FKey ProductionKeys[] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FLSCommandEntry Weapon;
		Weapon.Id = FName(*FString::Printf(TEXT("CastleWeapon%d"), Index));
		Weapon.Title = FText::FromString(WeaponNames[Index]);
		Weapon.Cell = 5 + Index;
		Weapon.Hotkey = WeaponKeys[Index];
		const auto Kind = static_cast<ELSCastleWeapon>(Index);
		FLSCastleWeaponState State;
		if (Castle->Weapons->GetWeaponState(Kind, State))
		{
			Weapon.Level = State.Level;
			Weapon.Remaining = Castle->Weapons->GetRemainingCooldown(Kind);
			Weapon.Duration = Castle->Weapons->GetCooldownDuration(Kind);
			Weapon.bEnabled = Castle->Weapons->GetStatus(Kind) == ELSWeaponCastResult::Ready;
			Weapon.Detail = FText::FromString(
			    !State.bUnlocked  ? TEXT("잠김")
			    : !State.bEnabled ? TEXT("비활성")
			    : PC->GetPendingInput() == ELSCommandInput::CastleWeapon && PC->GetPendingWeapon() == Kind
			        ? PC->GetLastWeaponResult() == ELSWeaponCastResult::OutOfRange ? TEXT("사거리 밖") : TEXT("조준 중")
			    : Weapon.Remaining > 0 ? TEXT("")
			    : Weapon.bEnabled      ? TEXT("사용 가능")
			                           : TEXT("사용 불가"));
		}
		Weapon.Tooltip = FText::FromString(TEXT("선택 후 전장 좌클릭으로 발사 · 우클릭/Esc 취소"));
		Result.Add(Weapon);
	}
	for (int32 Index = 0; Index < ALSTroopUnit::GetTroopTypeCount(); ++Index)
	{
		FLSCommandEntry Production;
		Production.Id = FName(*FString::Printf(TEXT("CastleProduce%d"), Index));
		Production.Title = FText::FromString(TroopNames[Index]);
		Production.Cell = 8 + Index;
		Production.Hotkey = ProductionKeys[Index];
		const auto Type = static_cast<ELSTroopType>(Index);
		const auto Status = PC->Production->GetStatus(Type);
		Production.bEnabled = Status == ELSProductionResult::Ready;
		Production.Remaining = PC->Production->GetRemainingCooldown(Type);
		Production.Duration = PC->Production->GetCooldownSeconds();
		Production.Detail = FText::FromString(Production.bEnabled                             ? TEXT("생산")
		                                      : Status == ELSProductionResult::PopulationFull ? TEXT("인구 한도")
		                                      : Status == ELSProductionResult::Cooldown       ? TEXT("")
		                                                                                      : TEXT("생산 불가"));
		if (Production.bEnabled && PC->GetLastProductionResult() == ELSProductionResult::NoSpace)
			Production.Detail = FText::FromString(TEXT("공간 부족"));
		Production.Tooltip = FText::FromString(TEXT("성 주변의 빈 공간에 생산 · 병종별 독립 쿨타임"));
		Result.Add(Production);
	}
	return Result;
}

bool ULSCastleCommandProviderComponent::ExecuteCommand_Implementation(FName Id, ALSPlayerController* PC)
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld()))
		return false;
	// 화면에 표시된 뒤 상태가 바뀌었더라도 실행 직전에 다시 검사한다.
	for (const auto& Entry : GetCommands_Implementation(PC))
		if (Entry.Id == Id && Entry.bEnabled)
		{
			if (Entry.Cell >= 8)
			{
				PC->ProduceTroop(static_cast<ELSTroopType>(Entry.Cell - 8));
				return PC->GetLastProductionResult() == ELSProductionResult::Ready;
			}
			return PC->ArmCastleWeapon(static_cast<ELSCastleWeapon>(Entry.Cell - 5));
		}
	return false;
}
