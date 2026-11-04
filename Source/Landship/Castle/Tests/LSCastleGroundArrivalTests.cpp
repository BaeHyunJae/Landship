#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/LSCastle.h"
#include "Castle/LSCastleAIController.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "AssetCompilingManager.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleGroundArrivalTest,
	"Landship.Castle.Movement.TurningGroundMoveReachesProjectedCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleGroundArrivalTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Body);
	Floor->AddInstanceComponent(Body);
	Body->SetBoxExtent(FVector(4000, 2000, 50));
	Body->SetMobility(EComponentMobility::Static);
	Body->SetCollisionObjectType(LSCollisionChannels::Ground);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetActorLocation(FVector(0, 0, -50));
	Body->RegisterComponent();
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(4100, 2100, 1500));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();
	const FVector Start(-1200, 0, 502);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, Start);
	ALSCastleAIController* AI = World->SpawnActor<ALSCastleAIController>();
	if (!TestNotNull(TEXT("실제 성"), Castle) || !TestNotNull(TEXT("성 경로 추종 컨트롤러"), AI)) return false;
	Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
	auto* Movement = CastChecked<ULSCastleMovementComponent>(Castle->GetMovementComponent());
	Movement->NavAgentProps.AgentRadius = 500;
	Movement->NavAgentProps.AgentHeight = 1000;
	AI->Possess(Castle);
	auto* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::GameMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Nav->Build();
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Castle->Orders->SetComponentTickEnabled(false);
	auto* Following = CastChecked<ULSCastlePathFollowingComponent>(AI->GetPathFollowingComponent());
	const ANavigationData* Data = Nav->GetNavDataForProps(Castle->GetNavAgentPropertiesRef(), Castle->GetNavAgentLocation());
	if (!TestNotNull(TEXT("성 크기의 실제 NavData"), Data)) return false;
	FNavLocation ProjectedGoal;
	if (!TestTrue(TEXT("클릭할 지면 목표를 실제 NavMesh에 투영"), Nav->ProjectPointToNavigation(
		FVector(1200, 0, 0), ProjectedGoal, FVector(100, 100, 250), Data))) return false;
	for (const bool bViaOrders : {false, true})
	{
		for (const bool bTurningMode : {false, true})
		{
			Castle->StopMoving();
			Castle->SetActorLocation(Start);
			Castle->SetActorRotation(FRotator::ZeroRotator);
			Movement->bTurnBeforeForward = bTurningMode;
			bool bAccepted;
			if (bViaOrders)
			{
				FLSOrder Order;
				Order.Kind = ELSOrderKind::Move;
				Order.Destination = ProjectedGoal.Location;
				bAccepted = Castle->Orders->SubmitOrder(Order);
			}
			else
			{
				bAccepted = Castle->RequestMove(ProjectedGoal.Location);
			}
			if (!TestTrue(TEXT("공개 지면 이동 요청을 실제 경로 탐색기가 수락"), bAccepted)) return false;
			bool bGradualBraking = false;
			bool bExcessDeceleration = false;
			for (int32 Index = 0; Index < 2500 && Following->GetStatus() != EPathFollowingStatus::Idle; ++Index)
			{
				const double BeforeSpeed = Movement->Velocity.Size2D();
				++GFrameCounter;
				World->Tick(LEVELTICK_All, .01f);
				const double Speed = Movement->Velocity.Size2D();
				if (BeforeSpeed > 100 && BeforeSpeed - Speed > 1) bGradualBraking = true;
				if (BeforeSpeed - Speed > Movement->ForwardBrakingDeceleration * .01 + 1.1) bExcessDeceleration = true;
			}
			const double Distance = FVector::Dist2D(Castle->GetNavAgentLocation(), ProjectedGoal.Location);
			const double AcceptanceRadius = bViaOrders ? 30 : 20;
			AddInfo(FString::Printf(TEXT("Ground arrival: Orders=%d Turning=%d Distance=%.3f Acceptance=%.0f Goal=%s Center=%s"),
				bViaOrders, bTurningMode, Distance, AcceptanceRadius, *ProjectedGoal.Location.ToString(), *Castle->GetActorLocation().ToString()));
			TestEqual(TEXT("실제 경로 추종 완료"), Following->GetStatus(), EPathFollowingStatus::Idle);
			TestTrue(TEXT("네이티브 경로 소유자가 도착으로 판정"), Following->DidMoveReachGoal());
			if (bTurningMode)
			{
				// 0.01초 이동 틱의 최대 전진량은 4cm다. 캡슐 반경을 오차에 넣지 않는다.
				TestTrue(TEXT("선회 성의 중심이 요청 허용 반경과 5cm 수치 오차 안에 도착"), Distance <= AcceptanceRadius + 5);
				TestTrue(TEXT("실제 중심점 도착 경계까지 점진 제동"), bGradualBraking);
				TestFalse(TEXT("중심점 도착 때 설정 제동률을 넘는 순간 정지가 없음"), bExcessDeceleration);
			}
			else
			{
				TestTrue(TEXT("기본 이동은 캡슐을 포함하므로 동일 클릭보다 5m 이상 앞에서 멈춤"), Distance > 500);
				TestTrue(TEXT("기본 이동의 기존 캡슐 포함 도착 경계를 유지"), Distance <= AcceptanceRadius + 555);
			}
		}
	}
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
