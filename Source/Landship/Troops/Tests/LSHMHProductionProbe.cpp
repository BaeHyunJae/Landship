#include "Troops/Tests/LSHMHProductionProbe.h"

#include "Castle/LSMobileCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSRTSHUD.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Match/LSGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ALSHMHProductionProbe::ALSHMHProductionProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSHMHProductionProbe::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHProductionTestExit")))
	{
		return;
	}
#endif
	SetActorTickEnabled(false);
	Destroy();
}

void ALSHMHProductionProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("HMH_PRODUCTION %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Message);
}

void ALSHMHProductionProbe::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("HMH_PRODUCTION_FINISHED failures=%d"), Failures);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSHMHProductionProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 30)
	{
		Check(false, TEXT("생산 검사 시간 제한"));
		Finish();
		return;
	}
	ALSPlayerController* Controller = Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController());
	ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	if (Stage == 0 && StageTime > 1.5f)
	{
		Check(Controller && State && Controller->GetHUD(), TEXT("생산 컨트롤러·공통 인구수·HUD 준비"));
		if (!Controller || !State || !Controller->GetHUD())
		{
			Finish();
			return;
		}
		ALSMobileCastle* EnemyCastle = nullptr;
		for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
		{
			It->Orders->SetComponentTickEnabled(false);
			It->BasicAttack->StopAttack();
			if (ALSMobileCastle* Found = Cast<ALSMobileCastle>(*It))
			{
				if (Found->GetFaction() == ELSFaction::Ally)
				{
					Castle = Found;
				}
				else
				{
					EnemyCastle = Found;
				}
			}
		}
		Check(Castle && EnemyCastle, TEXT("양 진영 성 배치"));
		if (!Castle || !EnemyCastle)
		{
			Finish();
			return;
		}
		Check(State->GetPopulationLimit() == 100 && State->GetCurrentPopulation() == 3,
		      TEXT("인구수 상한 100과 초기 아군 세 병력 집계"));
		Controller->ApplySelection({});
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::SelectCastle,
		      TEXT("성 미선택 생산 거부"));
		Controller->ApplySelection({EnemyCastle});
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::SelectCastle,
		      TEXT("적 성 생산 거부"));
		Controller->ApplySelection({Castle});
		State->SetCurrentPopulation(100);
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::PopulationFull,
		      TEXT("인구수 100에서 생산 차단"));
		Check(Controller->Production->GetRemainingCooldown(ELSTroopType::Spearman) == 0,
		      TEXT("인구수 부족 실패는 쿨타임 미소모"));
		State->SetCurrentPopulation(3);
		Castle->SetActorLocation(FVector(-900, -400, 144));
		AActor* Obstacle = GetWorld()->SpawnActor<AActor>();
		Blocker = NewObject<UBoxComponent>(Obstacle);
		Obstacle->SetRootComponent(Blocker);
		Obstacle->AddInstanceComponent(Blocker);
		Blocker->SetBoxExtent(FVector(500, 500, 180));
		Blocker->SetCollisionObjectType(LSCollisionChannels::PathObstacle);
		Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Blocker->SetCollisionResponseToAllChannels(ECR_Block);
		Blocker->SetCanEverAffectNavigation(false);
		Blocker->RegisterComponent();
		Obstacle->SetActorLocation(Castle->GetActorLocation());
		int32 Width, Height;
		Controller->GetViewportSize(Width, Height);
		ALSRTSHUD* HUD = Cast<ALSRTSHUD>(Controller->GetHUD());
		Check(HUD && HUD->HandleClick(FVector2D(FMath::Max(0, Width - 360) + 64, FMath::Max(0, Height - 180) + 95)),
		      TEXT("생산 버튼 클릭 처리"));
		ALSTroopUnit* Spear = Controller->Production->GetLastProducedUnit();
		Check(IsValid(Spear) && Spear->GetTroopType() == ELSTroopType::Spearman, TEXT("클릭으로 지정 병종 실제 생성"));
		if (!Spear)
		{
			Finish();
			return;
		}
		Check(FVector::Dist2D(Spear->GetActorLocation(), Castle->GetActorLocation()) > 500,
		      TEXT("성 주변이 막히면 더 먼 빈 공간 탐색"));
		Spear->Orders->SetComponentTickEnabled(false);
		Spear->BasicAttack->StopAttack();
		Check(Controller->GetSelectedActors().Contains(Castle), TEXT("생산 버튼은 성 선택 유지"));
		Check(State->GetCurrentPopulation() == 4, TEXT("생산 성공 시 인구수 1 증가"));
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::Cooldown &&
		          State->GetCurrentPopulation() == 4,
		      TEXT("같은 병종 연속 생산 거부"));
		Check(Controller->Production->Produce(ELSTroopType::Archer) == ELSProductionResult::Ready,
		      TEXT("다른 병종은 독립 쿨타임으로 즉시 생산"));
		Controller->Production->GetLastProducedUnit()->Orders->SetComponentTickEnabled(false);
		Check(Controller->Production->Produce(ELSTroopType::Cavalry) == ELSProductionResult::Ready,
		      TEXT("기마병 생산"));
		Controller->Production->GetLastProducedUnit()->Orders->SetComponentTickEnabled(false);
		Check(State->GetCurrentPopulation() == 6, TEXT("세 병종 생산 인구수 합산"));
		Controller->ApplySelection({});
		Controller->ApplySelection({Castle});
		Check(Controller->Production->GetRemainingCooldown(ELSTroopType::Spearman) > 9,
		      TEXT("선택 해제·재선택으로 쿨타임 초기화 불가"));
		Spear->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		    ULSHealthComponent::MakeDamageRequest(10000, EnemyCastle, EnemyCastle));
		Check(State->GetCurrentPopulation() == 5, TEXT("병력 사망 시 인구수 반환"));
		Spear->Destroy();
		Check(State->GetCurrentPopulation() == 5, TEXT("시체 제거로 인구수 중복 반환 방지"));
		Blocker->SetBoxExtent(FVector(6000, 6000, 2000));
		Controller->ConsoleCommand(TEXT("HighResShot 1600x900"));
		Stage = 1;
		StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 10.1f)
	{
		Check(Controller->Production->GetRemainingCooldown(ELSTroopType::Spearman) == 0,
		      TEXT("10초 경과 후 쿨타임 해제"));
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::NoSpace,
		      TEXT("탐색 범위 전체가 막힌 경우 생산 거부"));
		Check(State->GetCurrentPopulation() == 5 &&
		          Controller->Production->GetRemainingCooldown(ELSTroopType::Spearman) == 0,
		      TEXT("공간 부족 시 인구수·쿨타임 보존"));
		Blocker->GetOwner()->Destroy();
		Check(Controller->Production->Produce(ELSTroopType::Spearman) == ELSProductionResult::Ready,
		      TEXT("공간 확보 후 즉시 재생산"));
		Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		    ULSHealthComponent::MakeDamageRequest(10000, nullptr, nullptr));
		Check(Controller->Production->GetStatus(ELSTroopType::Archer) == ELSProductionResult::SelectCastle,
		      TEXT("성 파괴 후 생산 차단"));
		Finish();
	}
}
