#include "LSHMHSkeletonProbe.h"

#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Controls/LSPlayerController.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Match/LSGameState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
	constexpr ECollisionChannel Channels[] = {
		LSCollisionChannels::Unit, LSCollisionChannels::Building,
		LSCollisionChannels::PathObstacle, LSCollisionChannels::Projectile, LSCollisionChannels::Ground
	};
	const TCHAR* Labels[] = { TEXT("Unit"), TEXT("Building"), TEXT("Obstacle"), TEXT("Projectile"), TEXT("Ground") };
}

ALSHMHSkeletonProbe::ALSHMHSkeletonProbe()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(FName(Labels[Index]));
		Box->SetupAttachment(RootComponent);
		Box->SetRelativeLocation(FVector(Index * 250.0f, 0.0f, 100.0f));
		Box->SetBoxExtent(FVector(50.0f));
		Box->SetCollisionObjectType(Channels[Index]);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(LSCollisionChannels::Projectile, ECR_Block);
		Box->SetCanEverAffectNavigation(false);
		Targets.Add(Box);
	}
}

void ALSHMHSkeletonProbe::BeginPlay()
{
	Super::BeginPlay();
	// 월드와 컴포넌트 초기화 뒤 검사하여 시작 순서에 따른 오판을 피한다.
	GetWorldTimerManager().SetTimerForNextTick(this, &ALSHMHSkeletonProbe::RunChecks);
}

void ALSHMHSkeletonProbe::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (ALSGameState* State = GetWorld()->GetGameState<ALSGameState>())
	{
		State->OnPopulationChanged.RemoveDynamic(this, &ALSHMHSkeletonProbe::HandlePopulationChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void ALSHMHSkeletonProbe::HandlePopulationChanged(int32 CurrentPopulation, int32 PopulationLimit)
{
	Notifications.Emplace(CurrentPopulation, PopulationLimit);
}

void ALSHMHSkeletonProbe::Check(bool bPassed, const FString& Description)
{
	++CheckCount;
	FailureCount += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("[HMH Skeleton] %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *Description);
}

void ALSHMHSkeletonProbe::RunChecks()
{
	ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	Check(State != nullptr, TEXT("World uses ALSGameState"));
	Check(Cast<ALSPlayerController>(GetWorld()->GetFirstPlayerController()) != nullptr, TEXT("World uses ALSPlayerController"));
	if (State)
	{
		const FIntPoint Original(State->GetCurrentPopulation(), State->GetPopulationLimit());
		State->OnPopulationChanged.AddUniqueDynamic(this, &ALSHMHSkeletonProbe::HandlePopulationChanged);
		// 이전 상태와 무관하게 검사 기준을 만들고 그 초기화 알림은 집계하지 않는다.
		State->SetPopulation(0, 0);
		Notifications.Reset();
		Check(State->SetPopulation(2, 5) && Notifications.Num() == 1 && Notifications.Last() == FIntPoint(2, 5), TEXT("Initialize 2/5: receiver gets one event"));
		Check(State->ChangePopulation(3) && Notifications.Num() == 2 && Notifications.Last() == FIntPoint(5, 5), TEXT("Increase to 5/5: receiver gets updated values"));
		Check(State->SetPopulation(5, 5) && Notifications.Num() == 2, TEXT("Same values: no extra event"));
		Check(!State->ChangePopulation(1) && State->GetCurrentPopulation() == 5 && Notifications.Num() == 2, TEXT("Over limit: unchanged, no event"));
		Check(!State->SetPopulationLimit(4) && State->GetPopulationLimit() == 5 && Notifications.Num() == 2, TEXT("Invalid limit: unchanged, no event"));
		Check(!State->ChangePopulation(-6) && Notifications.Num() == 2, TEXT("Negative population rejected without event"));
		Check(State->ChangePopulation(-2) && Notifications.Num() == 3 && Notifications.Last() == FIntPoint(3, 5), TEXT("Decrease to 3/5: receiver notified"));
		Check(State->SetPopulationLimit(6) && Notifications.Num() == 4 && Notifications.Last() == FIntPoint(3, 6), TEXT("Limit change: receiver notified"));
		State->OnPopulationChanged.RemoveDynamic(this, &ALSHMHSkeletonProbe::HandlePopulationChanged);
		State->SetPopulation(Original.X, Original.Y);
	}

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		UBoxComponent* Box = Targets[Index];
		const FVector Center = Box->GetComponentLocation();
		const FVector Start = Center - FVector(0.0f, 180.0f, 0.0f);
		const FVector End = Center + FVector(0.0f, 180.0f, 0.0f);
		FCollisionObjectQueryParams ObjectQuery;
		ObjectQuery.AddObjectTypesToQuery(Channels[Index]);
		FHitResult ObjectHit;
		const bool bObjectHit = GetWorld()->LineTraceSingleByObjectType(ObjectHit, Start, End, ObjectQuery);
		Check(bObjectHit && ObjectHit.GetComponent() == Box, FString::Printf(TEXT("%s object channel: line trace hits correct box"), Labels[Index]));

		FHitResult ProjectileHit;
		const bool bProjectileHit = GetWorld()->SweepSingleByChannel(ProjectileHit, Start, End, FQuat::Identity,
			LSCollisionChannels::Projectile, FCollisionShape::MakeSphere(10.0f));
		Check(bProjectileHit && ProjectileHit.GetComponent() == Box, FString::Printf(TEXT("Projectile channel: sphere sweep blocked by %s"), Labels[Index]));
		DrawDebugBox(GetWorld(), Center, Box->GetScaledBoxExtent(), FColor::Cyan, false, 600.0f, 0, 3.0f);
		DrawDebugLine(GetWorld(), Start, End, FColor::Yellow, false, 600.0f, 0, 2.0f);
		DrawDebugString(GetWorld(), Center + FVector(0, 0, 80), FString(Labels[Index]), nullptr, FColor::White, 600.0f);
	}
	Targets[0]->SetCollisionResponseToChannel(LSCollisionChannels::Projectile, ECR_Ignore);
	const FVector Center = Targets[0]->GetComponentLocation();
	FHitResult IgnoreHit;
	Check(!GetWorld()->SweepSingleByChannel(IgnoreHit, Center - FVector(0, 180, 0), Center + FVector(0, 180, 0),
		FQuat::Identity, LSCollisionChannels::Projectile, FCollisionShape::MakeSphere(10)), TEXT("Projectile Ignore response: sweep passes through"));
	Targets[0]->SetCollisionResponseToChannel(LSCollisionChannels::Projectile, ECR_Block);

	const FString Summary = FString::Printf(TEXT("HMH Skeleton: %d/%d PASS | population events + object traces + projectile sweeps"), CheckCount - FailureCount, CheckCount);
	UE_LOG(LogTemp, Display, TEXT("[HMH Skeleton] %s"), *Summary);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 600.0f, FailureCount == 0 ? FColor::Green : FColor::Red, Summary);
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 600.0f, FColor::White, TEXT("No troops needed. See Output Log: HMH Skeleton. Population restored after test."));
	}
	// 자동 실행일 때만 종료하며, 일반 PIE에서는 사용자가 결과를 관찰한다.
	if (FParse::Param(FCommandLine::Get(), TEXT("HMHSkeletonTestExit")))
	{
		FPlatformMisc::RequestExitWithStatus(false, FailureCount == 0 ? 0 : 1);
	}
}
