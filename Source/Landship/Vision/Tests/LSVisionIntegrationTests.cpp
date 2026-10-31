#if WITH_DEV_AUTOMATION_TESTS

#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
ALSBasicAttackTestActor* SpawnCombatant(UWorld* World, ELSFaction Faction, FVector Location)
{
	if (!ALSBattlefieldSettings::Find(World))
	{
		World->SpawnActor<ALSBattlefieldSettings>();
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSBasicAttackTestActor* Actor =
		World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator, SpawnParameters);
	if (Actor)
	{
		Actor->SetInitialFaction(Faction);
		Actor->Attack->SetAutoAttackOnBeginPlay(false);
		Actor->HealthComponent->bDestroyOnDeath = false;
	}
	return Actor;
}

ULSVisionParticipantComponent* AddParticipant(AActor* Actor, bool bProvidesSight = false, float Radius = 0.0f)
{
	ULSVisionParticipantComponent* Participant = NewObject<ULSVisionParticipantComponent>(Actor);
	Participant->bProvidesSight = bProvidesSight;
	Participant->SetSightRadius(Radius);
	Actor->AddInstanceComponent(Participant);
	Participant->RegisterComponent();
	return Participant;
}

bool HasMarkerAt(const ULSVisionSubsystem* Vision, FVector Location, bool bPublic = false)
{
	for (const FLSMinimapMarker& Marker : Vision->GetMinimapMarkers())
	{
		if (Marker.Location.Equals(Location) && static_cast<bool>(Marker.bPublicPosition) == bPublic)
		{
			return true;
		}
	}
	return false;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionLifecycleTest, "Landship.Vision.Lifecycle.SharedSightAndDiscovery",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnCombatant(World, ELSFaction::Ally, FVector::ZeroVector);
	ALSBasicAttackTestActor* Target = SpawnCombatant(World, ELSFaction::Enemy, FVector(300, 400, 9000));
	if (!TestNotNull(TEXT("아군 생성"), Source) || !TestNotNull(TEXT("적 생성"), Target))
	{
		return false;
	}
	ULSVisionParticipantComponent* SourceVision = AddParticipant(Source);
	ULSVisionParticipantComponent* TargetVision = AddParticipant(Target);
	TargetVision->bTriggersEnemyDiscovery = true;
	TargetVision->MarkerKind = ELSMinimapMarkerKind::Unit;
	UPrimitiveComponent* TargetPrimitive = CastChecked<UPrimitiveComponent>(Target->GetRootComponent());
	TargetPrimitive->SetVisibility(false);
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야 서브시스템"), Vision))
	{
		return false;
	}
	Vision->MarkDiscoveryCaptionDisplayed();
	TestFalse(TEXT("시야원 없이 적 은폐"), Vision->IsActorVisibleToFriendly(Target));
	TestTrue(TEXT("렌더링도 은폐"), Target->IsHidden());
	TestFalse(TEXT("발견 전 자막 없음"), Vision->HasPendingDiscoveryCaption());
	SourceVision->bProvidesSight = true;
	SourceVision->SetSightRadius(500);
	TestEqual(TEXT("아군 시야원 하나"), Vision->GetSightCircles().Num(), 1);
	TestTrue(TEXT("Z와 무관한 XY 원 둘레 포함"), Vision->IsActorVisibleToFriendly(Target));
	TestFalse(TEXT("시야 안 액터 은폐 해제"), Target->IsHidden());
	TestFalse(TEXT("원래 꺼진 컴포넌트 가시성 보존"), TargetPrimitive->IsVisible());
	TestTrue(TEXT("공유 시야로 점 조회"), Vision->IsPointVisible(FVector(300, 400, -9000)));
	TestTrue(TEXT("최초 발견 저장"), Vision->HasDiscoveredEnemy());
	TestTrue(TEXT("UI 표시 전 자막 대기"), Vision->HasPendingDiscoveryCaption());
	TestTrue(TEXT("보이는 적 지도 표시"), HasMarkerAt(Vision, Target->GetActorLocation()));
	Target->SetActorLocation(FVector(301, 400, 9000));
	Vision->RequestRefresh();
	TestFalse(TEXT("경계 밖 이동 시 재은폐"), Vision->IsActorVisibleToFriendly(Target));
	TestFalse(TEXT("숨은 적 지도에서 제거"), HasMarkerAt(Vision, Target->GetActorLocation()));
	TestTrue(TEXT("은폐 후에도 자막 대기 보존"), Vision->HasPendingDiscoveryCaption());
	Vision->MarkDiscoveryCaptionDisplayed();
	Target->SetActorLocation(FVector(300, 400, 9000));
	Vision->RequestRefresh();
	TestTrue(TEXT("재진입 시 적 재표시"), Vision->IsActorVisibleToFriendly(Target));
	TestFalse(TEXT("재발견으로 자막 반복하지 않음"), Vision->HasPendingDiscoveryCaption());
	TestEqual(TEXT("적 사망 피해 적용"), Target->ApplyTestDamage(100, Source).Status,
			  ELSApplyDamageStatus::Applied);
	TargetVision->NotifyCombatDeath();
	TestFalse(TEXT("사체는 전투 마커 없음"), HasMarkerAt(Vision, Target->GetActorLocation()));
	Target->SetActorLocation(FVector(1000, 0, 9000));
	Vision->RequestRefresh();
	TestTrue(TEXT("시야 밖 사체도 은폐"), Target->IsHidden());
	Target->SetActorLocation(FVector(100, 0, 9000));
	Vision->RequestRefresh();
	TestFalse(TEXT("시야 안 사체는 표시"), Target->IsHidden());
	TestFalse(TEXT("사체 재진입도 마커 없음"), HasMarkerAt(Vision, Target->GetActorLocation()));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionSourceLifetimeTest, "Landship.Vision.Lifecycle.SourcesAndRemoval",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionSourceLifetimeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnCombatant(World, ELSFaction::Ally, FVector::ZeroVector);
	ALSBasicAttackTestActor* SecondSource = SpawnCombatant(World, ELSFaction::Ally, FVector(1000, 0, 0));
	ALSBasicAttackTestActor* Target = SpawnCombatant(World, ELSFaction::Enemy, FVector(500, 0, 0));
	if (!Source || !SecondSource || !Target)
	{
		AddError(TEXT("전투 대역 생성 실패"));
		return false;
	}
	ULSVisionParticipantComponent* SourceVision = AddParticipant(Source, true, 600);
	AddParticipant(SecondSource, true, 600);
	AddParticipant(Target);
	AActor* PlainActor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("일반 액터 생성"), PlainActor))
	{
		return false;
	}
	AddParticipant(PlainActor);
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야 서브시스템"), Vision))
	{
		return false;
	}
	TestEqual(TEXT("일반 액터는 시야원 아님"), Vision->GetSightCircles().Num(), 2);
	TestTrue(TEXT("겹친 아군 시야 공유"), Vision->IsActorVisibleToFriendly(Target));
	TestEqual(TEXT("시야원 사망 피해 적용"), Source->ApplyTestDamage(100, Target).Status,
			  ELSApplyDamageStatus::Applied);
	SourceVision->NotifyCombatDeath();
	TestEqual(TEXT("사망 즉시 시야 제거"), Vision->GetSightCircles().Num(), 1);
	TestTrue(TEXT("다른 시야원이 계속 공개"), Vision->IsActorVisibleToFriendly(Target));
	TWeakObjectPtr<AActor> WeakSource(SecondSource);
	const uint64 PreviousGeneration = Vision->GetSnapshotGeneration();
	TestTrue(TEXT("남은 시야원 제거"), SecondSource->Destroy());
	TestTrue(TEXT("EndPlay가 갱신 유발"), Vision->GetSnapshotGeneration() > PreviousGeneration);
	TestEqual(TEXT("제거된 시야원 원 없음"), Vision->GetSightCircles().Num(), 0);
	TestTrue(TEXT("마지막 시야원 제거 후 은폐"), Target->IsHidden());
	TestFalse(TEXT("제거된 대상 조회 거절"), Vision->IsActorVisibleToFriendly(WeakSource.Get()));
	TestTrue(TEXT("적 제거"), Target->Destroy());
	Vision->RequestRefresh();
	TArray<AActor*> Concealed;
	Vision->GetConcealedActors(Concealed);
	TestFalse(TEXT("제거된 대상 은폐 목록에서 제외"), Concealed.Contains(Target));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSVisionPublicCoreTest, "Landship.Vision.Lifecycle.PublicCorePosition",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSVisionPublicCoreTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("월드 생성"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Core = SpawnCombatant(World, ELSFaction::Enemy, FVector(7000, 8000, 0));
	if (!TestNotNull(TEXT("공개 거점 생성"), Core))
	{
		return false;
	}
	ULSVisionParticipantComponent* Participant = AddParticipant(Core);
	Participant->bPublicCorePosition = true;
	Participant->MarkerKind = ELSMinimapMarkerKind::Building;
	if (!TestTrue(TEXT("BeginPlay"), TestWorld.BeginPlayInTestWorld()))
	{
		return false;
	}
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("시야 서브시스템"), Vision))
	{
		return false;
	}
	TestTrue(TEXT("시야 밖 공개 거점 좌표 표시"), HasMarkerAt(Vision, Core->GetActorLocation(), true));
	TestFalse(TEXT("공개 좌표가 액터 시야를 공개하지 않음"), Vision->IsActorVisibleToFriendly(Core));
	TestTrue(TEXT("공개 거점 본체는 은폐"), Core->IsHidden());
	TestEqual(TEXT("공개 마커는 시야를 제공하지 않음"), Vision->GetSightCircles().Num(), 0);
	TestEqual(TEXT("거점 피해 적용"), Core->ApplyTestDamage(100, nullptr).Status,
			  ELSApplyDamageStatus::Applied);
	Participant->NotifyCombatDeath();
	TestTrue(TEXT("사망 여부와 무관한 공개 위치"), HasMarkerAt(Vision, Core->GetActorLocation(), true));
	TestFalse(TEXT("공개 위치만으로 적 발견하지 않음"), Vision->HasDiscoveredEnemy());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
