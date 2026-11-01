#include "Obstacles/Tests/LSObstaclePresentationProbe.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSTurret.h"
#include "Troops/LSTroopUnit.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSRTSCameraPawn.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ALSObstaclePresentationProbe::ALSObstaclePresentationProbe()
{
	PrimaryActorTick.bCanEverTick = true;
}
void ALSObstaclePresentationProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("ObstaclePresentationTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
		return;
	}
	ScreenshotPath =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Review/TASK-47/obstacle-presentation.png"));
}
void ALSObstaclePresentationProbe::Finish(bool bPassed, const TCHAR* Reason)
{
	if (bFinished)
		return;
	bFinished = true;
	UE_LOG(LogTemp, Display, TEXT("OBSTACLE_PRESENTATION %s reason=%s file=%s bytes=%lld elapsed=%.2f"),
		   bPassed ? TEXT("PASS") : TEXT("FAIL"), Reason, *ScreenshotPath,
		   IFileManager::Get().FileSize(*ScreenshotPath), Elapsed);
	if (Scout.IsValid())
		Scout->Destroy();
	if (Mine.IsValid())
		Mine->SetHovered(false);
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}
void ALSObstaclePresentationProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
		return;
	Elapsed += DeltaSeconds;
	if (Elapsed > 20)
	{
		Finish(false, TEXT("timeout"));
		return;
	}
	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
	if (!bInitialized)
	{
		ALSRTSCameraPawn* View = Player ? Cast<ALSRTSCameraPawn>(Player->GetPawn()) : nullptr;
		if (!View)
			return;
		for (TActorIterator<ALSTurret> It(GetWorld()); It; ++It)
			It->StopObstacleActions();
		double Nearest = TNumericLimits<double>::Max();
		for (TActorIterator<ALSMine> It(GetWorld()); It; ++It)
		{
			const double Distance = FVector::DistSquared(It->GetTriggerCenter(), FVector(600, 2300, 0));
			if (Distance < Nearest && It->IsArmed())
			{
				Nearest = Distance;
				Mine = *It;
			}
		}
		if (!Mine.IsValid())
		{
			Finish(false, TEXT("missing showcase mine"));
			return;
		}
		Camera = View;
		View->Zoom((View->GetZoomDistance() - 2400) / 200);
		const FTransform SpawnTransform(FRotator::ZeroRotator, Mine->GetTriggerCenter() + FVector(0, -300, 88));
		ALSTroopUnit* Unit =
			GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), SpawnTransform, nullptr, nullptr,
														 ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			Finish(false, TEXT("scout spawn failed"));
			return;
		}
		Unit->SetInitialTroopType(ELSTroopType::Spearman);
		Unit->FinishSpawning(SpawnTransform);
		Scout = Unit;
		Unit->BasicAttack->StopAttack();
		Unit->Vision->SetSightRadius(2000);
		GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
		Mine->AddTickPrerequisiteActor(this);
		bInitialized = true;
	}
	if (!Mine.IsValid() || !Scout.IsValid() || !Camera.IsValid())
	{
		Finish(false, TEXT("presentation fixture removed"));
		return;
	}
	Camera->FocusLocation(Mine->GetTriggerCenter());
	Mine->SetHovered(true);
	if (!bSubmitted && Elapsed >= .5f)
	{
		FLSOrder Order;
		Order.Kind = ELSOrderKind::Disarm;
		Order.Target = Mine.Get();
		bSubmitted = true;
		if (!Scout->Orders->SubmitOrder(Order))
		{
			Finish(false, TEXT("disarm rejected"));
			return;
		}
		AcceptedAt = Elapsed;
	}
	if (bSubmitted && !bScreenshotRequested && Elapsed - AcceptedAt >= 3)
	{
		bool bProgressVisible = false;
		float Fill = 0;
		TInlineComponentArray<UStaticMeshComponent*> Components(Mine.Get());
		for (const UStaticMeshComponent* Component : Components)
		{
			if (Component->GetFName() == TEXT("ProgressFill"))
			{
				bProgressVisible = Component->IsVisible();
				Fill = Component->GetRelativeScale3D().X;
			}
		}
		ULSVisionSubsystem* Vision = GetWorld()->GetSubsystem<ULSVisionSubsystem>();
		int32 MineMarkers = 0;
		float Radius = 0;
		for (const FLSMinimapMarker& Marker : Vision->GetMinimapMarkers())
		{
			if (Marker.Location.Equals(Mine->GetTriggerCenter()) && Marker.ExplosionRadius > 0)
			{
				++MineMarkers;
				Radius = Marker.ExplosionRadius;
			}
		}
		FVector Closest;
		const float Distance =
			Scout->GetCapsuleComponent()->GetClosestPointOnCollision(Mine->GetTriggerCenter(), Closest);
		int32 Width = 0, Height = 0;
		Player->GetViewportSize(Width, Height);
		UE_LOG(
			LogTemp, Display,
			TEXT("OBSTACLE_PRESENTATION capture distance=%.2f order=%d progressVisible=%d fill=%.3f duration=%.1f "
				 "contactRadius=%.1f mineMarkers=%d radius=%.1f enemyDiscovered=%d camera=%s zoom=%.1f viewport=%dx%d"),
			Distance, static_cast<int32>(Scout->Orders->GetOrderKind()), bProgressVisible, Fill,
			Mine->GetDisarmDuration(), Mine->GetTriggerRadius(), MineMarkers, Radius, Vision->HasDiscoveredEnemy(),
			*Camera->GetActorLocation().ToString(), Camera->GetZoomDistance(), Width, Height);
		if (!bProgressVisible || Fill <= .01f || Fill >= 1 || MineMarkers != 1 ||
			Scout->Orders->GetOrderKind() != ELSOrderKind::Disarm || Width <= 0 || Height <= 0)
		{
			Finish(false, TEXT("capture state invalid"));
			return;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
		if (IFileManager::Get().FileExists(*ScreenshotPath) && !IFileManager::Get().Delete(*ScreenshotPath))
		{
			Finish(false, TEXT("cannot replace prior screenshot"));
			return;
		}
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
		bScreenshotRequested = true;
	}
	if (bScreenshotRequested && IFileManager::Get().FileSize(*ScreenshotPath) > 0)
		Finish(true, TEXT("screenshot written; visual review required"));
}
