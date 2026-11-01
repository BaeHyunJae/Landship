#include "Enemies/Tests/LSEnemyVisionTestLibrary.h"
#include "Controls/LSPlayerController.h"
#include "Enemies/Tests/LSEnemySupportTestAccess.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Enemies/LSOutpost.h"
#include "Vision/LSVisionSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Logging/StructuredLog.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Enemies/Tests/LSEnemyCombatProbe.h"
#include "EngineUtils.h"

bool ULSEnemyVisionTestLibrary::TrySelect(APlayerController* Controller, AActor* Target)
{
	ALSPlayerController* Player = Cast<ALSPlayerController>(Controller);
	if (!Player || !IsValid(Target)) { return false; }
	Player->ApplySelection({Target});
	return Player->GetSelectedActors().Contains(Target);
}

int32 ULSEnemyVisionTestLibrary::PendingSupportCount(AActor* Target)
{
#if WITH_DEV_AUTOMATION_TESTS
	const ULSEnemyBehaviorComponent* Behavior = Target ? Target->FindComponentByClass<ULSEnemyBehaviorComponent>() : nullptr;
	return Behavior ? FLSEnemySupportTestAccess::Pending(Behavior) : -1;
#else
	return -1;
#endif
}

ALSEnemyVisionRuntimeProbe::ALSEnemyVisionRuntimeProbe()
{
	PrimaryActorTick.bCanEverTick = true;
}

bool ULSEnemyVisionTestLibrary::TryAttackOrder(AActor* Source, AActor* Target)
{
	ULSOrderComponent* Orders = Source ? Source->FindComponentByClass<ULSOrderComponent>() : nullptr;
	if (!Orders) { return false; }
	FLSOrder Order;
	Order.Kind = ELSOrderKind::AttackTarget;
	Order.Target = Target;
	const bool bAccepted = Orders->SubmitOrder(Order);
	Orders->SubmitOrder(FLSOrder());
	return bAccepted;
}

bool ULSEnemyVisionTestLibrary::TryBasicAttack(AActor* Source, AActor* Target)
{
	ULSBasicAttackComponent* Attack = Source ? Source->FindComponentByClass<ULSBasicAttackComponent>() : nullptr;
	if (!Attack) { return false; }
	const bool bAccepted = Attack->StartAttack(Target);
	Attack->StopAttack();
	return bAccepted;
}

void ALSEnemyVisionRuntimeProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("LSEnemyRequireVision"))) { Destroy(); return; }
	for (TActorIterator<ALSEnemyCombatProbe> It(GetWorld()); It; ++It)
	{
		It->OnBeforePublicAttackOrderEvent.AddUObject(this, &ThisClass::InspectSubmission);
	}
}

void ALSEnemyVisionRuntimeProbe::InspectSubmission(AActor* Source, AActor* Target)
{
	const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	const bool bEnabled = Vision && Vision->IsVisionEnabled();
	const bool bVisible = bEnabled && Vision->IsActorVisibleToFriendly(Target);
	const bool bPasses = Source && Target && ULSVisionSubsystem::PassesFriendlySight(Source, Target);
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_VISION_SUBMIT enabled={Enabled} targetVisible={Visible} passes={Passes}",
		("Function", __FUNCTION__), ("Enabled", bEnabled), ("Visible", bVisible), ("Passes", bPasses));
	if (!bEnabled || !bVisible || !bPasses) { FPlatformMisc::RequestExitWithStatus(false, 1); }
}

void ALSEnemyVisionRuntimeProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetWorld()->GetTimeSeconds() < 0.5f) { return; }
	const ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
	const ULSBattlefieldStateSubsystem* Battle = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	const bool bEnabled = Vision && Vision->IsVisionEnabled();
	const bool bVisible = bEnabled && Battle && Vision->IsActorVisibleToFriendly(Battle->GetCoreOutpost());
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_VISION_ATTACK_GATE enabled={Enabled} coreVisible={Visible}",
		("Function", __FUNCTION__), ("Enabled", bEnabled), ("Visible", bVisible));
	if (!bEnabled || !bVisible) { FPlatformMisc::RequestExitWithStatus(false, 1); }
	SetActorTickEnabled(false);
}
