#include "Enemies/LSBattlefieldStateSubsystem.h"

#include "Combat/LSHealthComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging/StructuredLog.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogLSBattlefield, Log, All);

bool ULSBattlefieldStateSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void ULSBattlefieldStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ALSBattlefieldSetup* Setup = nullptr;
	int32 Count = 0;
	for (TActorIterator<ALSBattlefieldSetup> It(&InWorld); It; ++It)
	{
		Setup = *It;
		++Count;
	}
	if (Count == 0)
	{
		bool bUsesEnemyBehavior = false;
		for (TActorIterator<AActor> It(&InWorld); It; ++It)
		{
			bUsesEnemyBehavior |= It->FindComponentByClass<ULSEnemyBehaviorComponent>() != nullptr;
		}
		// 이 기능을 쓰지 않는 기존 전투 월드에는 전장 설정을 요구하지 않는다.
		if (!bUsesEnemyBehavior) { return; }
	}
	const FString Error = Count == 1 ? Setup->GetConfigurationError()
		: FString::Printf(TEXT("Expected exactly one battlefield setup; found %d"), Count);
	if (!Error.IsEmpty())
	{
		UE_LOGFMT(LogLSBattlefield, Warning, "[{Function}] Invalid battlefield setup: {Reason}",
			("Function", __FUNCTION__), ("Reason", Error));
		return;
	}
	PlayerCastle = Setup->PlayerCastle.Get();
	CastleHealth = Setup->PlayerCastle->FindComponentByClass<ULSHealthComponent>();
	CoreOutpost = Setup->CoreOutpost.Get();
	bSetupValid = true;
	bAutomaticAlliedTroops = Setup->bAutomaticAlliedTroops;
	CastleHealth->OnDiedEvent.AddUniqueDynamic(this, &ThisClass::OnCastleDied);
	PlayerCastle->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnCastleEndPlay);
	ObserveCastleDeath();
}

bool ULSBattlefieldStateSubsystem::TryGetPlayerCastleState(FLSPlayerCastleState& Out) const
{
	FLSHealthSnapshot Health;
	if (!bSetupValid || !PlayerCastle.IsValid() || !CastleHealth.IsValid() || !CastleHealth->TryGetHealth(Health))
	{
		return false;
	}
	FLSPlayerCastleState State;
	State.Castle = PlayerCastle;
	State.Location = PlayerCastle->GetActorLocation();
	State.bIsDead = Health.bIsDead;
	bCastleDeathObserved |= !!Health.bIsDead;
	Out = State;
	return true;
}

ALSOutpost* ULSBattlefieldStateSubsystem::GetCoreOutpost() const
{
	return bSetupValid ? CoreOutpost.Get() : nullptr;
}

void ULSBattlefieldStateSubsystem::ObserveCastleDeath() const
{
	FLSHealthSnapshot Health;
	if (CastleHealth.IsValid() && CastleHealth->TryGetHealth(Health) && Health.bIsDead)
	{
		bCastleDeathObserved = true;
	}
}

bool ULSBattlefieldStateSubsystem::IsBattleEndedForEnemies() const
{
	ObserveCastleDeath();
	return bCastleDeathObserved || bCoreCaptured;
}

bool ULSBattlefieldStateSubsystem::HasObservedPlayerCastleDeath() const
{
	ObserveCastleDeath();
	return bCastleDeathObserved;
}

void ULSBattlefieldStateSubsystem::OnCastleDied(const FLSDamageResult& Damage)
{
	bCastleDeathObserved = true;
}

void ULSBattlefieldStateSubsystem::OnCastleEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	// 제거 자체는 사망이 아니다. 필수 사망 정리 중 제거된 경우의 체력만 확인한다.
	ObserveCastleDeath();
}

void ULSBattlefieldStateSubsystem::RecordOutpostCaptured(ALSOutpost* Outpost)
{
	if (!bSetupValid || bCoreCaptured || !Outpost || Outpost != CoreOutpost.Get())
	{
		return;
	}
	CoreCapture.Outpost = Outpost;
	CoreCapture.OutpostName = Outpost->GetFName();
	CoreCapture.Location = Outpost->GetActorLocation();
	CoreCapture.bIsCoreOutpost = true;
	bCoreCaptured = true;
	CaptureNotificationTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ThisClass::BroadcastCoreCapture));
}

bool ULSBattlefieldStateSubsystem::TryGetCoreCapture(FLSOutpostCaptureSnapshot& Out) const
{
	if (!bCoreCaptured)
	{
		return false;
	}
	Out = CoreCapture;
	return true;
}

void ULSBattlefieldStateSubsystem::BroadcastCoreCapture()
{
	if (bCoreCaptured && !bCaptureBroadcast)
	{
		bCaptureBroadcast = true;
		OnCoreOutpostCapturedEvent.Broadcast(CoreCapture);
	}
}

void ULSBattlefieldStateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureNotificationTimer);
	}
	if (CastleHealth.IsValid())
	{
		CastleHealth->OnDiedEvent.RemoveDynamic(this, &ThisClass::OnCastleDied);
	}
	if (PlayerCastle.IsValid())
	{
		PlayerCastle->OnEndPlay.RemoveDynamic(this, &ThisClass::OnCastleEndPlay);
	}
	OnCoreOutpostCapturedEvent.Clear();
	PlayerCastle.Reset();
	CastleHealth.Reset();
	CoreOutpost.Reset();
	CoreCapture = FLSOutpostCaptureSnapshot();
	bSetupValid = false;
	bCastleDeathObserved = false;
	bCoreCaptured = false;
	bCaptureBroadcast = false;
	Super::Deinitialize();
}
