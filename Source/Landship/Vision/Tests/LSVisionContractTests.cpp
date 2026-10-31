#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastle.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSRTSHUD.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Match/LSGameMode.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"
#include "Vision/LSMinimapWidget.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"

namespace
{
ULSVisionParticipantComponent* AddContractParticipant(AActor* Owner)
{
	ULSVisionParticipantComponent* Component = NewObject<ULSVisionParticipantComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void StopContractAutomation(AActor* Actor)
{
	if (ULSBasicAttackComponent* Attack = Actor->FindComponentByClass<ULSBasicAttackComponent>())
	{
		Attack->SetAutoAttackOnBeginPlay(false);
		Attack->StopAttack();
	}
	if (ULSOrderComponent* Orders = Actor->FindComponentByClass<ULSOrderComponent>())
	{
		Orders->SetComponentTickEnabled(false);
	}
}

bool HasPositiveOverlap(const FBox2D& A, const FBox2D& B)
{
	return FMath::Min(A.Max.X, B.Max.X) > FMath::Max(A.Min.X, B.Min.X) + 0.001 &&
		   FMath::Min(A.Max.Y, B.Max.Y) > FMath::Max(A.Min.Y, B.Min.Y) + 0.001;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionNativeDeathContractTest,
								 "Landship.Vision.Contract.NativeSourceDeath",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionNativeDeathContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("월드 생성"), Fixture.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	if (!TestNotNull(TEXT("전장 설정"), World->SpawnActor<ALSBattlefieldSettings>()))
	{
		return false;
	}
	ALSMobileCastle* Mobile = World->SpawnActor<ALSMobileCastle>(FVector(0, 0, 300), FRotator::ZeroRotator);
	UClass* CastleClass =
		LoadClass<ALSCastle>(nullptr, TEXT("/Game/Developers/BHJ/CombatStats/BP_TestCastle.BP_TestCastle_C"));
	if (!TestNotNull(TEXT("실제 성 BP"), CastleClass) || !TestNotNull(TEXT("실제 이동 성"), Mobile))
	{
		return false;
	}
	ALSCastle* Castle =
		World->SpawnActor<ALSCastle>(CastleClass, FVector(1500, 0, 300), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("성 생성"), Castle))
	{
		return false;
	}
	Mobile->SetInitialFaction(ELSFaction::Ally);
	Castle->SetInitialFaction(ELSFaction::Ally);
	StopContractAutomation(Mobile);
	StopContractAutomation(Castle);
	Mobile->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
	Castle->FindComponentByClass<ULSHealthComponent>()->bDestroyOnDeath = false;
	if (!TestTrue(TEXT("BeginPlay"), Fixture.BeginPlayInTestWorld()))
	{
		return false;
	}
	StopContractAutomation(Mobile);
	StopContractAutomation(Castle);
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야"), Vision))
	{
		return false;
	}
	TestTrue(TEXT("명시적 전장 시야 활성화"), Vision->IsVisionEnabled());
	FLSUnitStatRow MobileStats;
	FLSCastleStatRow CastleStats;
	if (!TestTrue(TEXT("이동 성 수치 준비"), Mobile->TryGetStats(MobileStats)) ||
		!TestTrue(TEXT("성 수치 준비"), Castle->TryGetStats(CastleStats)))
	{
		return false;
	}
	TestEqual(TEXT("이동 성 수치 반경 전달"), Mobile->Vision->GetSightRadius(), MobileStats.VisionRadius);
	TestEqual(TEXT("성 수치 반경 전달"), Castle->Vision->GetSightRadius(), CastleStats.VisionRadius);
	TestTrue(TEXT("실제 수치 반경 양수"), MobileStats.VisionRadius > 0 && CastleStats.VisionRadius > 0);
	TestEqual(TEXT("실제 성 두 개가 시야 제공"), Vision->GetSightCircles().Num(), 2);
	uint64 Generation = Vision->GetSnapshotGeneration();
	ULSHealthComponent* Health = Mobile->FindComponentByClass<ULSHealthComponent>();
	TestEqual(
		TEXT("실제 이동 성 사망 피해"),
		Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(MobileStats.Health + 1, nullptr, nullptr))
			.Status,
		ELSApplyDamageStatus::Applied);
	TestTrue(TEXT("이동 성 필수 사망 훅에서 즉시 갱신"), Vision->GetSnapshotGeneration() > Generation);
	TestEqual(TEXT("수동 통지 없이 이동 성 시야 제거"), Vision->GetSightCircles().Num(), 1);
	Generation = Vision->GetSnapshotGeneration();
	Health = Castle->FindComponentByClass<ULSHealthComponent>();
	TestEqual(
		TEXT("실제 성 사망 피해"),
		Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(CastleStats.Health + 1, nullptr, nullptr))
			.Status,
		ELSApplyDamageStatus::Applied);
	TestTrue(TEXT("성 필수 사망 훅에서 즉시 갱신"), Vision->GetSnapshotGeneration() > Generation);
	TestEqual(TEXT("수동 통지 없이 성 시야 제거"), Vision->GetSightCircles().Num(), 0);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionLegacyMapContractTest, "Landship.Vision.Contract.UnconfiguredMap",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionLegacyMapContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("월드 생성"), Fixture.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	if (!TestNotNull(TEXT("적 대역"), Enemy))
	{
		return false;
	}
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	StopContractAutomation(Enemy);
	AddContractParticipant(Enemy);
	if (!TestTrue(TEXT("BeginPlay"), Fixture.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야"), Vision))
	{
		return false;
	}
	TestFalse(TEXT("설정 없는 맵 시야 비활성화"), Vision->IsVisionEnabled());
	TestFalse(TEXT("기존 맵 적 렌더링 보존"), Enemy->IsHidden());
	TestTrue(TEXT("기존 맵 적 조회 허용"), Vision->IsActorVisibleToFriendly(Enemy));
	const ALSGameMode* Mode = GetDefault<ALSGameMode>();
	TestTrue(TEXT("게임 모드의 HUD 연결"), Mode->HUDClass == ALSRTSHUD::StaticClass());
	const FClassProperty* WidgetClass =
		FindFProperty<FClassProperty>(ALSRTSHUD::StaticClass(), TEXT("MinimapWidgetClass"));
	if (TestNotNull(TEXT("미니맵 클래스 속성"), WidgetClass))
	{
		TestNotNull(TEXT("실제 기본 HUD 위젯 BP 연결"),
					WidgetClass->GetObjectPropertyValue_InContainer(GetDefault<ALSRTSHUD>()));
	}
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionMapAuthoringContractTest, "Landship.Vision.Contract.MapAuthoring",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionMapAuthoringContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("월드 생성"), Fixture.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	ALSBattlefieldSettings* Settings = World->SpawnActor<ALSBattlefieldSettings>();
	ALSBasicAttackTestActor* MissingEnemy = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* ConfiguredEnemy = World->SpawnActor<ALSBasicAttackTestActor>();
	AActor* Obstacle = World->SpawnActor<AActor>();
	if (!Settings || !MissingEnemy || !ConfiguredEnemy || !Obstacle)
	{
		AddError(TEXT("설정 검사 대역 생성 실패"));
		return false;
	}
	MissingEnemy->SetInitialFaction(ELSFaction::Enemy);
	ConfiguredEnemy->SetInitialFaction(ELSFaction::Enemy);
	AddContractParticipant(ConfiguredEnemy);
	UBoxComponent* Collision = NewObject<UBoxComponent>(Obstacle);
	Obstacle->AddInstanceComponent(Collision);
	Obstacle->SetRootComponent(Collision);
	Collision->SetCollisionObjectType(LSCollisionChannels::PathObstacle);
	Collision->RegisterComponent();
	TArray<AActor*> Missing = Settings->FindUnconfiguredParticipants();
	TestTrue(TEXT("누락된 적 발견"), Missing.Contains(MissingEnemy));
	TestTrue(TEXT("경로 장애물 채널 누락 발견"), Missing.Contains(Obstacle));
	TestFalse(TEXT("설정된 적 제외"), Missing.Contains(ConfiguredEnemy));
	AddContractParticipant(MissingEnemy);
	AddContractParticipant(Obstacle);
	TestTrue(TEXT("모든 역할 설정 후 누락 없음"), Settings->FindUnconfiguredParticipants().IsEmpty());
	TestTrue(TEXT("단일 유효 설정 발견"), ALSBattlefieldSettings::Find(World) == Settings);
	const FVector2D OriginalMax = Settings->BoundsMax;
	Settings->BoundsMax = Settings->BoundsMin;
	TestNull(TEXT("크기 0 경계 거절"), ALSBattlefieldSettings::Find(World));
	Settings->BoundsMax = OriginalMax;
	ALSBattlefieldSettings* Duplicate = World->SpawnActor<ALSBattlefieldSettings>();
	if (!TestNotNull(TEXT("중복 설정 생성"), Duplicate))
	{
		return false;
	}
	TestNull(TEXT("중복 설정 거절"), ALSBattlefieldSettings::Find(World));
	Duplicate->BoundsMax = Duplicate->BoundsMin;
	TestNull(TEXT("중복 중 하나가 잘못되어도 거절"), ALSBattlefieldSettings::Find(World));
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionHUDLayoutContractTest, "Landship.Vision.Contract.HUDLayout",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionHUDLayoutContractTest::RunTest(const FString& Parameters)
{
	for (FVector2D Viewport :
		 {FVector2D(1920, 1080), FVector2D(960, 540), FVector2D(640, 360), FVector2D(320, 180)})
	{
		const FLSBattlefieldHUDLayout Layout = FLSBattlefieldHUDLayout::Calculate(Viewport, true);
		for (const FBox2D& Rect : {Layout.Minimap, Layout.Info, Layout.Production})
		{
			TestTrue(TEXT("패널 양의 크기"), Rect.bIsValid && Rect.GetSize().X > 0 && Rect.GetSize().Y > 0);
			TestTrue(TEXT("패널 화면 안"), Rect.Min.X >= -0.001 && Rect.Min.Y >= -0.001 &&
											   Rect.Max.X <= Viewport.X + 0.001 &&
											   Rect.Max.Y <= Viewport.Y + 0.001);
		}
		TestFalse(TEXT("미니맵과 정보 비중첩"), HasPositiveOverlap(Layout.Minimap, Layout.Info));
		TestFalse(TEXT("미니맵과 생산 비중첩"), HasPositiveOverlap(Layout.Minimap, Layout.Production));
		TestFalse(TEXT("정보와 생산 비중첩"), HasPositiveOverlap(Layout.Info, Layout.Production));
		TestTrue(TEXT("미니맵 정사각형 보존"),
				 FMath::IsNearlyEqual(Layout.Minimap.GetSize().X, Layout.Minimap.GetSize().Y, 0.001));
		TestTrue(TEXT("정보 패널 크기 비율 보존"),
				 Layout.Info.GetSize().Equals(FVector2D(340, 125) * Layout.Scale, 0.001));
		TestTrue(TEXT("생산 패널 크기 비율 보존"),
				 Layout.Production.GetSize().Equals(FVector2D(360, 180) * Layout.Scale, 0.001));
	}
	const FLSBattlefieldHUDLayout Typical = FLSBattlefieldHUDLayout::Calculate(FVector2D(1920, 1080), true);
	TestTrue(TEXT("일반 화면 원래 지도 크기"), Typical.Minimap.GetSize().Equals(FVector2D(260, 260)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionPauseContractTest, "Landship.Vision.Contract.PauseAndGameTime",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionPauseContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("월드 생성"), Fixture.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	ALSBattlefieldSettings* Settings = World->SpawnActor<ALSBattlefieldSettings>();
	ALSMobileCastle* Source = World->SpawnActor<ALSMobileCastle>();
	APlayerState* Pauser = World->SpawnActor<APlayerState>();
	if (!Settings || !Source || !Pauser)
	{
		AddError(TEXT("일시정지 대역 생성 실패"));
		return false;
	}
	Settings->VisionRefreshInterval = 0.1f;
	StopContractAutomation(Source);
	if (!TestTrue(TEXT("BeginPlay"), Fixture.BeginPlayInTestWorld()))
	{
		return false;
	}
	StopContractAutomation(Source);
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야"), Vision))
	{
		return false;
	}
	const uint64 Generation = Vision->GetSnapshotGeneration();
	// Tick에는 엔진이 전달하는 게임 시간 간격을 넣어 실시간 타이머 없이 경계를 검사한다.
	Vision->Tick(0.09f);
	TestEqual(TEXT("0.1초 이전 스냅샷 보존"), Vision->GetSnapshotGeneration(), Generation);
	Vision->Tick(0.011f);
	TestTrue(TEXT("0.1초 경계 이후 갱신"), Vision->GetSnapshotGeneration() > Generation);
	World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	if (!TestTrue(TEXT("실제 엔진 일시정지 상태"), World->IsPaused()))
	{
		return false;
	}
	const uint64 PausedGeneration = Vision->GetSnapshotGeneration();
	const int32 PausedCircleCount = Vision->GetSightCircles().Num();
	ULSHealthComponent* Health = Source->FindComponentByClass<ULSHealthComponent>();
	TestEqual(TEXT("정지 중 실제 사망 피해"),
			  Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(100000, nullptr, nullptr)).Status,
			  ELSApplyDamageStatus::Applied);
	Vision->Tick(1.0f);
	TestEqual(TEXT("정지 중 사망 통지와 Tick은 스냅샷 동결"), Vision->GetSnapshotGeneration(),
			  PausedGeneration);
	TestEqual(TEXT("정지 중 시야 데이터 보존"), Vision->GetSightCircles().Num(), PausedCircleCount);
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	TestFalse(TEXT("실제 엔진 정지 해제"), World->IsPaused());
	Vision->Tick(0.001f);
	TestTrue(TEXT("해제 후 대기 갱신 반영"), Vision->GetSnapshotGeneration() > PausedGeneration);
	TestEqual(TEXT("해제 후 사망 시야 제거"), Vision->GetSightCircles().Num(), 0);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionSelectionContractTest,
								 "Landship.Vision.Contract.SelectionAndAttachments",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionSelectionContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("월드 생성"), Fixture.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = Fixture.GetTestWorld();
	World->SpawnActor<ALSBattlefieldSettings>();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Target =
		World->SpawnActor<ALSBasicAttackTestActor>(FVector(50, 0, 0), FRotator::ZeroRotator);
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	AStaticMeshActor* Visual = World->SpawnActor<AStaticMeshActor>();
	if (!Source || !Target || !Controller || !Visual)
	{
		AddError(TEXT("선택 대역 생성 실패"));
		return false;
	}
	Source->SetInitialFaction(ELSFaction::Ally);
	Target->SetInitialFaction(ELSFaction::Enemy);
	StopContractAutomation(Source);
	StopContractAutomation(Target);
	ULSVisionParticipantComponent* Sight = AddContractParticipant(Source);
	Sight->bProvidesSight = true;
	Sight->SetSightRadius(100);
	ULSVisionParticipantComponent* TargetVision = AddContractParticipant(Target);
	ULSSelectableComponent* Selection = NewObject<ULSSelectableComponent>(Target);
	Target->AddInstanceComponent(Selection);
	Selection->RegisterComponent();
	Visual->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	TestTrue(TEXT("시각 액터 부착"),
			 Visual->AttachToActor(Target, FAttachmentTransformRules::KeepWorldTransform));
	if (!TestTrue(TEXT("BeginPlay"), Fixture.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	Controller->ApplySelection({Target});
	TestEqual(TEXT("보이는 적 정보 선택"), Controller->GetSelectedActors().Num(), 1);
	Sight->SetSightRadius(10);
	TestTrue(TEXT("시야 밖 부착 시각도 은폐"), Visual->IsHidden());
	TestTrue(TEXT("시야 상실 순간 선택 제거"), Controller->GetSelectedActors().IsEmpty());
	Sight->SetSightRadius(100);
	TestTrue(TEXT("같은 프레임 재발견도 자동 재선택 없음"), Controller->GetSelectedActors().IsEmpty());
	TestFalse(TEXT("재발견 시 부착 시각 복원"), Visual->IsHidden());
	Source->SetActorHiddenInGame(true);
	Target->SetActorHiddenInGame(true);
	Vision->RequestRefresh();
	TestTrue(TEXT("아군의 다른 숨김 사유 보존"), Source->IsHidden());
	TestTrue(TEXT("보이는 적의 다른 숨김 사유 보존"), Target->IsHidden());
	TArray<AActor*> Attached;
	TargetVision->GetAttachedVisualActors(Attached);
	TestTrue(TEXT("커서와 렌더링이 공유할 부착 목록"), Attached.Contains(Visual));
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
