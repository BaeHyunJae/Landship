#if WITH_DEV_AUTOMATION_TESTS
#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSPlayerController.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Vision/Tests/LSMinimapInputTestWidget.h"
#include "Vision/LSVisionGeometry.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMinimapCastleIntegrationTest, "Landship.Vision.Minimap.CastleBlindFire",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMinimapCastleIntegrationTest::RunTest(const FString& Parameters)
{
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game))
			return false;
		UWorld* World = Fixture.GetTestWorld();
		World->SpawnActor<ALSBattlefieldSettings>();
		ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
		ALSMobileCastle* Castle = World->SpawnActor<ALSMobileCastle>();
		ALSBasicAttackTestActor* Enemy =
		    World->SpawnActor<ALSBasicAttackTestActor>(FVector(400, 0, 0), FRotator::ZeroRotator);
		Enemy->SetInitialFaction(ELSFaction::Enemy);
		Enemy->HealthComponent->bDestroyOnDeath = false;
		Enemy->Attack->SetAutoAttackOnBeginPlay(false);
		ULSVisionParticipantComponent* TargetVision = NewObject<ULSVisionParticipantComponent>(Enemy);
		Enemy->AddInstanceComponent(TargetVision);
		TargetVision->RegisterComponent();
		if (!Fixture.BeginPlayInTestWorld())
			return false;
		Castle->Vision->SetSightRadius(50);
		ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
		Vision->RequestRefresh();
		Vision->Tick(0.11f);
		TestTrue(TEXT("시야 활성"), Vision->IsVisionEnabled());
		TestFalse(TEXT("적은 시야 밖"), Vision->IsActorVisibleToFriendly(Enemy));
		TestFalse(TEXT("빈 공간의 일반 좌클릭은 카메라 담당"),
		          Controller->HandleMinimapInput(FVector(400, 0, 0), false, false));
		ULSMinimapInputTestWidget* Widget = CreateWidget<ULSMinimapInputTestWidget>(World);
		ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
		Player->PlayerController = Controller;
		Controller->Player = Player;
		Widget->SetPlayerContext(FLocalPlayerContext(Player, World));
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(260, 260), FSlateLayoutTransform(1.0f));
		FVector2D UV;
		LSVisionGeometry::WorldToUV(FVector2D(400, 0), ALSBattlefieldSettings::Find(World)->GetWorldBounds(), UV);
		const FBox2D MapRect = LSVisionGeometry::FitMapRect(FBox2D(FVector2D::ZeroVector, FVector2D(260, 260)),
		                                                    ALSBattlefieldSettings::Find(World)->GetWorldBounds());
		const FVector2D Position = MapRect.Min + UV * MapRect.GetSize();
		FPointerEvent Click(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0,
		                    FModifierKeysState());
		Controller->ApplySelection({Castle});
		TestTrue(TEXT("무장 조준"), Controller->ArmCastleWeapon(Weapon));
		TestTrue(TEXT("맵 밖 입력 거절"), !Controller->HandleMinimapInput(FVector(9000, 0, 0), false, false));
		TestTrue(TEXT("거절 시 조준 유지"), Controller->GetPendingInput() == ELSCommandInput::CastleWeapon);
		TestTrue(TEXT("미니맵 우클릭 취소"),
		         Widget
		             ->NativeOnMouseButtonDown(Geometry,
		                                       FPointerEvent(0, Position, Position, TSet<FKey>{EKeys::RightMouseButton},
		                                                     EKeys::RightMouseButton, 0, FModifierKeysState()))
		             .IsEventHandled());
		TestEqual(TEXT("취소 시 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(Weapon), 0.0f);
		Controller->ArmCastleWeapon(Weapon);
		TestTrue(TEXT("안개 밖 지점 발사"), Widget->NativeOnMouseButtonDown(Geometry, Click).IsEventHandled());
		for (int32 I = 0; I < 80; ++I)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.01f);
		}
		TestTrue(TEXT("은폐된 적도 무장 피해 적용"), Enemy->GetCurrentHealth() < 100);
		TestTrue(TEXT("발사 쿨타임 시작"), Castle->Weapons->GetRemainingCooldown(Weapon) > 0);
	}
	return true;
}
#endif
