#include "Controls/Tests/LSHMHCombatOrdersProbe.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Castle/LSCastle.h"
#include "Troops/LSUnit.h"
#include "Troops/LSTroopUnit.h"
#include "Vision/LSVisionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/Navigation/AvoidanceManager.h"

namespace
{
float ReadProbeHP(AActor* Unit)
{
	FLSHealthSnapshot S;
	return Unit->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(S) ? S.CurrentHealth : -1;
}
void HaltProbeUnit(ALSUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
	if (auto* AI = Cast<AAIController>(Unit->GetController()))
		AI->StopMovement();
}
} // namespace
ALSHMHCombatOrdersProbe::ALSHMHCombatOrdersProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}
void ALSHMHCombatOrdersProbe::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHCombatOrdersTestExit")) ||
	    FParse::Param(FCommandLine::Get(), TEXT("HMHMeleeAvoidanceTestExit")))
		return;
#endif
	SetActorTickEnabled(false);
	Destroy();
}
void ALSHMHCombatOrdersProbe::Check(bool bOK, const TCHAR* Message)
{
	Failures += bOK ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("HMH_ORDERS %s: %s"), bOK ? TEXT("PASS") : TEXT("FAIL"), Message);
}
void ALSHMHCombatOrdersProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_ORDERS_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
void ALSHMHCombatOrdersProbe::OnSpear(AActor* Target, float Damage)
{
	if (Target == TargetCastle && Damage > 0)
		++Hits[0];
}
void ALSHMHCombatOrdersProbe::OnArcher(AActor* Target, float Damage)
{
	if (Target == TargetCastle && Damage > 0)
		++Hits[1];
}
void ALSHMHCombatOrdersProbe::OnCavalry(AActor* Target, float Damage)
{
	if (Target == TargetCastle && Damage > 0)
		++Hits[2];
}
void ALSHMHCombatOrdersProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHMeleeAvoidanceTestExit")))
	{
		TickMeleeAvoidanceProbe();
		return;
	}
	auto* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Elapsed > 35)
	{
		Check(false, TEXT("교전 명령 검사 시간 제한"));
		Finish();
		return;
	}
	if (Stage == 0 && Elapsed > 1.5f)
	{
		Army.SetNum(3);
		for (TActorIterator<ALSCastle> It(GetWorld()); It; ++It)
			if (It->GetFaction() == ELSFaction::Enemy) { TargetCastle = *It; It->Orders->SetComponentTickEnabled(false); }
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			HaltProbeUnit(*It);
			if (auto* Unit = Cast<ALSTroopUnit>(*It))
			{
				if (Unit->GetFaction() == ELSFaction::Ally)
					Army[static_cast<int32>(Unit->GetTroopType())] = Unit;
				else
				{
					Enemy = Unit;
					Unit->SetActorLocation(FVector(1500, 1500, 96));
				}
			}
		}
		if (!Controller || !TargetCastle || !Enemy || !Army[0] || !Army[1] || !Army[2])
		{
			Check(false, TEXT("검사 병력 배치"));
			Finish();
			return;
		}
		TargetCastle->SetActorLocation(FVector(600, -1100, 144));
		for (int32 I = 0; I < 3; ++I)
		{
			Army[I]->SetActorLocation(FVector(-900 - I * 120, -1050 - I * 110, 96));
			Army[I]->Orders->SetComponentTickEnabled(true);
		}
		Army[0]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHCombatOrdersProbe::OnSpear);
		Army[1]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHCombatOrdersProbe::OnArcher);
		Army[2]->BasicAttack->OnAttackPerformed.AddDynamic(this, &ALSHMHCombatOrdersProbe::OnCavalry);
		auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
		Vision->RequestRefresh();
		Vision->Tick(0.11f);
		Controller->ApplySelection({Army[0], Army[1], Army[2]});
		Check(Controller->IssueSelectionOrder(ELSOrderKind::AttackTarget, FVector::ZeroVector, TargetCastle, false),
		      TEXT("3병종 공통 지정 공격"));
		if (auto* Camera = Cast<ALSRTSCameraPawn>(Controller->GetPawn()))
			Camera->FocusLocation(FVector(400, -1100, 0));
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && Hits[0] > 0 && Hits[1] > 0 && Hits[2] > 0)
	{
		Check(true, TEXT("창병·궁병·기병 모두 같은 대상을 실제 타격"));
		Check(ULSCombatTargetLibrary::GetAttackDistance(Army[0], TargetCastle) <=
		          Army[0]->BasicAttack->GetAttackSettings().Range,
		      TEXT("큰 대상 외곽에서 근접 교전"));
		GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("HighResShot 1600x900"));
		for (int32& Hit : Hits)
			Hit = 0;
		TargetCastle->SetActorLocation(FVector(1100, -1100, 144));
		Stage = 2;
		StageTime = 0;
	}
	else if (Stage == 2 && Hits[0] > 0 && Hits[1] > 0 && Hits[2] > 0)
	{
		Check(true, TEXT("이동한 표적을 재추적해 다시 타격"));
		Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(-1000, -1300, 0), nullptr, true),
		      TEXT("교전 중 이동 예약"));
		for (ALSTroopUnit* Unit : Army)
			Check(Unit->Orders->GetQueuedCount() == 1, TEXT("각 병력 예약 보관"));
		TargetCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		    ULSHealthComponent::MakeDamageRequest(10000, Army[0], Army[0]));
		Stage = 3;
		StageTime = 0;
	}
	else if (Stage == 3 && StageTime > 0.15f)
	{
		for (ALSTroopUnit* Unit : Army)
		{
			Check(Unit->Orders->GetOrderKind() == ELSOrderKind::Move && Unit->Orders->GetQueuedCount() == 0,
			      TEXT("표적 사망 후 예약 이동 시작"));
		}
		LastPosition = Army[0]->GetActorLocation();
		Stage = 4;
		StageTime = 0;
	}
	else if (Stage == 4 && StageTime > 0.5f)
	{
		Check(FVector::Dist2D(LastPosition, Army[0]->GetActorLocation()) > 25, TEXT("예약 이동 실제 진행"));
		for (ALSTroopUnit* Unit : Army)
			HaltProbeUnit(Unit);
		Controller->ApplySelection({Army[0]});
		Army[0]->Orders->SetComponentTickEnabled(true);
		Enemy->SetActorLocation(Army[0]->GetActorLocation() + FVector(200, 0, 0));
		LastPosition = Army[0]->GetActorLocation();
		auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
		Vision->RequestRefresh();
		Vision->Tick(0.11f);
		Controller->IssueSelectionOrder(ELSOrderKind::Hold, FVector::ZeroVector, nullptr, false);
		Stage = 5;
		StageTime = 0;
	}
	else if (Stage == 5 && StageTime > 1.2f)
	{
		Check(ReadProbeHP(Enemy) < 120, TEXT("위치 사수 중 사거리 안 적 공격"));
		Check(FVector::Dist2D(LastPosition, Army[0]->GetActorLocation()) < 5, TEXT("위치 사수 중 추적 이동 없음"));
		BaselineHealth = ReadProbeHP(Enemy);
		Check(Controller->IssueSelectionOrder(ELSOrderKind::Move, FVector(-1400, -1300, 0), nullptr, false),
		      TEXT("교전 대신 일반 이동"));
		Stage = 6;
		StageTime = 0;
	}
	else if (Stage == 6 && StageTime > 0.5f)
	{
		Check(ReadProbeHP(Enemy) == BaselineHealth, TEXT("일반 이동 중 자동 공격 중단"));
		Controller->IssueSelectionOrder(ELSOrderKind::Idle, FVector::ZeroVector, nullptr, false);
		Check(Army[0]->Orders->GetOrderKind() == ELSOrderKind::Idle && Army[0]->Orders->GetQueuedCount() == 0,
		      TEXT("정지로 명령·예약 취소"));
		Finish();
	}
}

void ALSHMHCombatOrdersProbe::TickMeleeAvoidanceProbe()
{
	if (Elapsed > 20)
	{
		Check(false, TEXT("근접 회피 검사 시간 제한"));
		Finish();
		return;
	}
	if (Stage == 0 && Elapsed > 1.5f)
	{
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			HaltProbeUnit(*It);
			It->GetCharacterMovement()->DisableMovement();
			It->SetActorLocation(FVector(2200, 2200, 500));
			It->SetActorEnableCollision(false);
		}
		const bool bDisableRVO = FParse::Param(FCommandLine::Get(), TEXT("MeleeProbeNoRVO"));
		int32 Count = 5;
		FParse::Value(FCommandLine::Get(), TEXT("MeleeProbeCount="), Count);
		Count = FMath::Clamp(Count, 1, 5);
		float Range = 250;
		FParse::Value(FCommandLine::Get(), TEXT("MeleeProbeRange="), Range);
		for (int32 Side = 0; Side < 2; ++Side)
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FTransform Spawn(FRotator::ZeroRotator, FVector(Side ? 550 : -550, -1300 + Index * 110, 100));
				auto* Unit =
				    GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Spawn, nullptr, nullptr,
				                                                 ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Unit)
				{
					Check(false, TEXT("근접 검사 병력 생성"));
					Finish();
					return;
				}
				Unit->SetInitialTroopType(Index % 2 ? ELSTroopType::Cavalry : ELSTroopType::Spearman);
				Unit->SetInitialFaction(Side ? ELSFaction::Enemy : ELSFaction::Ally);
				Unit->FinishSpawning(Spawn);
				auto Settings = Unit->BasicAttack->GetAttackSettings();
				Settings.Damage = 1;
				Settings.Interval = 0.2f;
				Settings.Range = Range;
				Unit->BasicAttack->ConfigureAttack(Settings);
				if (bDisableRVO)
					Unit->GetCharacterMovement()->AvoidanceConsiderationRadius = 0;
				if (Side && FParse::Param(FCommandLine::Get(), TEXT("MeleeProbeStaticTargets")))
					Unit->Orders->SetComponentTickEnabled(false);
				MeleeUnits.Add(Unit);
			}
		auto* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
		Vision->RequestRefresh();
		Vision->Tick(0.11f);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FLSOrder Order;
			Order.Kind = ELSOrderKind::AttackTarget;
			Order.Target = MeleeUnits[Index + Count];
			Check(MeleeUnits[Index]->Orders->SubmitOrder(Order), TEXT("근접 집단 공격 요청 접수"));
		}
		Stage = 1;
		StageTime = 0;
		UE_LOG(LogTemp, Display, TEXT("MELEE_PROBE start range=%.1f expansion=%.2f noRVO=%d"), Range,
		       GetWorld()->GetAvoidanceManager()->ArtificialRadiusExpansion, bDisableRVO);
	}
	if (Stage != 1)
		return;
	const int32 Count = MeleeUnits.Num() / 2;
	if (FParse::Param(FCommandLine::Get(), TEXT("MeleeProbeNoRVO")))
		for (ALSTroopUnit* Unit : MeleeUnits)
			Unit->GetCharacterMovement()->AvoidanceConsiderationRadius = 0;
	if (StageTime >= NextMeleeLog)
	{
		NextMeleeLog = StageTime + 1;
		for (int32 Index = 0; Index < Count; ++Index)
			UE_LOG(LogTemp, Display, TEXT("MELEE_PROBE t=%.2f unit=%d pos=%s vel=%s gap=%.2f targetHP=%.1f failure=%d"),
			       StageTime, Index, *MeleeUnits[Index]->GetActorLocation().ToCompactString(),
			       *MeleeUnits[Index]->GetVelocity().ToCompactString(),
			       ULSCombatTargetLibrary::GetAttackDistance(MeleeUnits[Index], MeleeUnits[Index + Count]),
			       ReadProbeHP(MeleeUnits[Index + Count]),
			       static_cast<int32>(MeleeUnits[Index]->Orders->GetLastFailure()));
	}
	if (StageTime > 9)
	{
		for (int32 Index = Count; Index < Count * 2; ++Index)
			Check(ReadProbeHP(MeleeUnits[Index]) < 115, TEXT("각 근접 표적에 지속적인 실제 피해 적용"));
		Finish();
	}
}
